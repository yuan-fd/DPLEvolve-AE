#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

CASE_ID="aes_nangate45"
FLOW_VARIANT="paper9_place"
THREADS="${THREADS:-8}"

usage() {
  cat <<'EOF'
Usage: validate_evaluator.sh [--case ID] [--flow-variant NAME] [--threads N] [--dry-run]

Run all three canonical detailed-placement baselines on one prepared ODB and
produce fresh H_g/H_lg/H_ip/H_f, legality, displacement, and runtime metrics.
This checks the protected measurement path; it is not a paper-result claim.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --case) CASE_ID="$2"; shift 2 ;;
    --flow-variant) FLOW_VARIANT="$2"; shift 2 ;;
    --threads) THREADS="$2"; shift 2 ;;
    --dry-run) REPRO_DRY_RUN=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) repro_die "unknown argument: $1" ;;
  esac
done

repro_positive_integer threads "${THREADS}"
if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then
  repro_require_runtime
fi

repro_run "${DPL_EVOLVE_AGENT_ROOT}/baseline/run_baseline_suite.sh" \
  --case "${CASE_ID}" \
  --flow-variant "${FLOW_VARIANT}" \
  --threads "${THREADS}" \
  --tag-prefix "ae_evaluator_validation"
