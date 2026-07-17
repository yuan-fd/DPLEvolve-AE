#!/usr/bin/env bash
# DPLEvolve AE — Agent Experiment Execution
# Generic experiment runner with pre/post validation and logging.
#
# Usage:
#   execute_experiment.sh --config configs/paper/baseline_9case.yaml --experiment baseline

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AE_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
export AE_ROOT

# shellcheck source=/dev/null
source "${AE_ROOT}/scripts/lib/env_vars.sh"
dpl_ae_resolve_env

# shellcheck source=/dev/null
source "${AE_ROOT}/scripts/lib/utils.sh"

CONFIG=""
EXPERIMENT=""
DRY_RUN=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --config) CONFIG="$2"; shift 2 ;;
    --experiment) EXPERIMENT="$2"; shift 2 ;;
    --dry-run) DRY_RUN=1; shift ;;
    *) shift ;;
  esac
done

if [[ -z "${EXPERIMENT}" ]]; then
  dpl_ae_error "--experiment is required (smoke, baseline, bo_search, evolve_dse)"
  exit 2
fi

STAMP="$(dpl_ae_timestamp)"
RUN_DIR="${AE_ROOT}/results/reproduced/${EXPERIMENT}_${STAMP}"
mkdir -p "${RUN_DIR}"

echo "=============================================="
echo " DPLEvolve AE — Agent Experiment Execution"
echo "=============================================="
dpl_ae_info "Experiment: ${EXPERIMENT}"
dpl_ae_info "Config:     ${CONFIG:-default}"
dpl_ae_info "Run dir:    ${RUN_DIR}"
dpl_ae_info "Dry run:    ${DRY_RUN}"
echo ""

# --- Pre-flight ---
dpl_ae_info "Running pre-flight checks..."
bash "${SCRIPT_DIR}/inspect_environment.sh" "${RUN_DIR}/environment.json"

# Check input ODB if smoke test
if [[ "${EXPERIMENT}" == "smoke" ]]; then
  dpl_ae_info "Verifying AES input ODB checksum..."
  # This is done by the smoke test script internally
fi
echo ""

# --- Execute ---
START_TIME="$(date -Iseconds)"

if [[ "${DRY_RUN}" -eq 1 ]]; then
  dpl_ae_info "DRY RUN — would execute:"
  case "${EXPERIMENT}" in
    smoke)
      echo "  bash scripts/human/smoke_test.sh --run --threads 8"
      ;;
    baseline)
      echo "  bash scripts/human/reproduce_baseline.sh"
      ;;
    bo_search|evolve_dse)
      echo "  cd \$DPL_EVOLVE_AGENT_ROOT && source env.sh"
      echo "  bash experiments/launchers/run_evolve_9case_place_batch.sh"
      ;;
  esac
else
  dpl_ae_info "Executing experiment..."
  case "${EXPERIMENT}" in
    smoke)
      bash "${AE_ROOT}/scripts/human/smoke_test.sh" --run --threads 8
      ;;
    baseline)
      bash "${AE_ROOT}/scripts/human/reproduce_baseline.sh"
      ;;
    bo_search|evolve_dse)
      if [[ -z "${ANTHROPIC_API_KEY:-}" ]]; then
        dpl_ae_error "ANTHROPIC_API_KEY not set. Cannot run LLM experiments."
        exit 1
      fi
      bash "${AE_ROOT}/scripts/human/reproduce_main.sh"
      ;;
    *)
      dpl_ae_error "Unknown experiment: ${EXPERIMENT}"
      exit 2
      ;;
  esac
fi

END_TIME="$(date -Iseconds)"
EXIT_CODE=$?

# --- Post-flight ---
echo ""
dpl_ae_info "Experiment finished at ${END_TIME}"

# Write run manifest
cat > "${RUN_DIR}/run_manifest.json" <<JSON
{
  "run_id": "${EXPERIMENT}_${STAMP}",
  "experiment": "${EXPERIMENT}",
  "config": "${CONFIG:-default}",
  "start_time": "${START_TIME}",
  "end_time": "${END_TIME}",
  "exit_code": ${EXIT_CODE},
  "dry_run": ${DRY_RUN}
}
JSON

# Classify outcome
if [[ "${EXIT_CODE}" -eq 0 ]]; then
  echo "PASS" > "${RUN_DIR}/OUTCOME.txt"
  dpl_ae_ok "Experiment PASSED"
else
  echo "FAIL (exit code: ${EXIT_CODE})" > "${RUN_DIR}/OUTCOME.txt"
  dpl_ae_error "Experiment FAILED with exit code ${EXIT_CODE}"
fi

echo "Run manifest: ${RUN_DIR}/run_manifest.json"
exit ${EXIT_CODE}
