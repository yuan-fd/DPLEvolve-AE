#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

THREADS="${THREADS:-10}"
FLOW_VARIANT="${FLOW_VARIANT:-paper9_place}"
DELTA_TOLERANCE_PP="${ARIANE_DELTA_TOLERANCE_PP:-0.25}"
RUNTIME_TOLERANCE="${ARIANE_RUNTIME_RATIO_TOLERANCE:-0.20}"
CONFIG="${AE_ROOT}/configs/reproduction/ariane-diagnostic.tsv"
SOURCE_ROOT="${AE_ROOT}/artifacts/01-table4-qor/diagnostics/ariane-warmstart/programs"
RETAINED_ROOT="${AE_ROOT}/artifacts/01-table4-qor/inputs/diagnostics"
ONLY_LABEL=""

usage() {
  cat <<'EOF'
Usage: reproduce_ariane_diagnostic.sh [--label LABEL] [--threads N]
       [--flow-variant NAME] [--check-sources] [--dry-run]

Rebuild and replay the six exact source trees behind the Ariane133 warm-start
diagnostic footnote.  The command uses the protected full-flow evaluator and
derives both group means from fresh metrics; it does not call an LLM.
EOF
}

CHECK_ONLY=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --label) ONLY_LABEL="$2"; shift 2 ;;
    --threads) THREADS="$2"; shift 2 ;;
    --flow-variant) FLOW_VARIANT="$2"; shift 2 ;;
    --check-sources) CHECK_ONLY=1; shift ;;
    --dry-run) REPRO_DRY_RUN=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) repro_die "unknown argument: $1" ;;
  esac
done
repro_positive_integer threads "${THREADS}"

"${DPL_EVOLVE_PYTHON}" "${SCRIPT_DIR}/summarize_ariane_diagnostic.py" \
  --config "${CONFIG}" --source-root "${SOURCE_ROOT}" --check-sources \
  --retained-tsv "${RETAINED_ROOT}/ariane133_warmstart_smoke_diagnostic.tsv" \
  --retained-manifest "${RETAINED_ROOT}/MANIFEST.sha256"
if [[ "${CHECK_ONLY}" -eq 1 ]]; then
  exit 0
fi
if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then
  repro_require_runtime
fi

RUN_ID="ariane_diagnostic_$(date +%Y%m%d_%H%M%S)"
if [[ "${REPRO_DRY_RUN}" -eq 1 ]]; then
  RUN_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/dplevolve-ariane-dry-run.XXXXXX")"
  trap 'rm -rf "${RUN_ROOT}"' EXIT
else
  RUN_ROOT="${REPRO_OUTPUT_ROOT}/${RUN_ID}"
fi
MATRIX_ROOT="${RUN_ROOT}/matrices"
MANIFEST="${RUN_ROOT}/runs.tsv"
mkdir -p "${RUN_ROOT}"
printf 'label\tgroup\tresults_tsv\n' > "${MANIFEST}"

while IFS=$'\t' read -r label group _commit _tree _hpwl _delta _runtime _ratio; do
  [[ "${label}" == label ]] && continue
  [[ -n "${ONLY_LABEL}" && "${label}" != "${ONLY_LABEL}" ]] && continue
  matrix_id="${RUN_ID}_${label}"
  plan="${RUN_ROOT}/${label}.plan.tsv"
  printf 'enabled\tcase\tcore_utilization\tflow_variant\tround_id\tstart_kind\tnotes\n' > "${plan}"
  printf '1\tariane133_nangate45\tdefault\t%s\t%s\tfrozen_source\tAriane warm-start diagnostic replay\n' \
    "${FLOW_VARIANT}" "${matrix_id}" >> "${plan}"
  args=(
    --matrix-id "${matrix_id}"
    --output-root "${MATRIX_ROOT}"
    --candidate-src "${SOURCE_ROOT}/${label}/dpl_evolve"
    --candidate-label "ariane_diagnostic_${label}"
    --plan "${plan}"
    --threads "${THREADS}"
    --max-parallel 1
    --skip-place
    --skip-baseline
  )
  if [[ "${REPRO_DRY_RUN}" -eq 1 ]]; then args+=(--dry-run); fi
  repro_run "${DPL_EVOLVE_AGENT_ROOT}/scripts/matrix/run_candidate_matrix.sh" "${args[@]}"
  results="${MATRIX_ROOT}/${matrix_id}/results.tsv"
  printf '%s\t%s\t%s\n' "${label}" "${group}" "${results}" >> "${MANIFEST}"
done < "${CONFIG}"

if [[ -n "${ONLY_LABEL}" || "${REPRO_DRY_RUN}" -eq 1 ]]; then
  repro_note "partial/dry diagnostic complete; run all six labels for group means"
  exit 0
fi

DEFAULT_METRICS="${ORFS_ROOT}/flow/reports/nangate45/ariane133/${FLOW_VARIANT}/dpl_evolve_baseline/bo9_openroad_dpl_flow_ariane133_nangate45/metrics.json"
"${DPL_EVOLVE_PYTHON}" "${SCRIPT_DIR}/summarize_ariane_diagnostic.py" \
  --config "${CONFIG}" \
  --source-root "${SOURCE_ROOT}" \
  --run-manifest "${MANIFEST}" \
  --default-metrics "${DEFAULT_METRICS}" \
  --delta-tolerance-pp "${DELTA_TOLERANCE_PP}" \
  --runtime-ratio-tolerance "${RUNTIME_TOLERANCE}" \
  --output "${RUN_ROOT}/ariane-diagnostic-fresh.tsv"
