#!/usr/bin/env bash
# DPLEvolve AE — AES Smoke Test (Human Entry Point)
# Runs the minimal AES smoke test using the pinned environment.
# Usage: make smoke or artifacts/04-aes-smoke/run.sh --run

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AE_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
export AE_ROOT

MODE=""
FLOW_VARIANT=""
RUN_TAG=""
THREADS="8"

usage() {
  cat <<'EOF'
Usage: artifacts/04-aes-smoke/run.sh (--check-only | --run | --rebuild) [options]

Modes:
  --check-only   Validate an existing AES snapshot and baseline result.
  --run          Create a new timestamped snapshot and run the baseline.
  --rebuild      Same as --run but with a distinct rebuild tag.

Options:
  --flow-variant ID  Existing variant for --check-only, or new variant name.
  --run-tag ID       Baseline result tag.
  --threads N        ORFS/OpenROAD threads. Default: 8.
  --help             Show this message.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --check-only|--run|--rebuild)
      if [[ -n "${MODE}" ]]; then echo "[ERROR] Choose exactly one mode." >&2; exit 2; fi
      MODE="${1#--}"
      shift
      ;;
    --flow-variant|--run-tag|--threads)
      if [[ $# -lt 2 ]]; then echo "[ERROR] $1 requires a value." >&2; exit 2; fi
      case "$1" in
        --flow-variant) FLOW_VARIANT="$2" ;;
        --run-tag) RUN_TAG="$2" ;;
        --threads) THREADS="$2" ;;
      esac
      shift 2
      ;;
    --help|-h) usage; exit 0 ;;
    *) echo "[ERROR] Unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done
if [[ -z "${MODE}" ]]; then
  echo "[ERROR] Choose --check-only, --run, or --rebuild." >&2
  usage >&2
  exit 2
fi
if ! [[ "${THREADS}" =~ ^[1-9][0-9]*$ ]]; then
  echo "[ERROR] --threads must be a positive integer." >&2
  exit 2
fi

LOCK="${SCRIPT_DIR}/expected/ae_reproduction_lock.json"
if [[ ! -f "${LOCK}" ]]; then
  echo "[ERROR] Reproduction lock not found: ${LOCK}" >&2
  exit 1
fi

# A clean artifact clone intentionally does not include the large ORFS result
# tree.  The check-only action is an optional inspection of a locally prepared
# reference run, so report its absence as a skip rather than making the primary
# reviewer flow look broken.  Fresh runs remain strict and resolve the complete
# environment below.
requested_orfs_root="${ORFS_ROOT:-$(realpath -m "${AE_ROOT}/../OpenROAD-flow-scripts")}"
if [[ "${MODE}" == "check-only" && ! -d "${requested_orfs_root}/flow" ]]; then
  echo "[SKIP] Optional prepared AES smoke result is not available."
  echo "       A clean clone does not contain the large ORFS/ODB result tree."
  echo "       Run 'make bootstrap && make setup && make smoke' for a fresh validation."
  exit 0
fi

# shellcheck source=/dev/null
source "${AE_ROOT}/scripts/shared/env_vars.sh"
dpl_ae_resolve_env

# shellcheck source=/dev/null
source "${AE_ROOT}/scripts/shared/utils.sh"

read_lock() {
  "${DPL_EVOLVE_PYTHON}" -c 'import json,sys; data=json.load(open(sys.argv[1], encoding="utf-8")); value=data; [value := value[key] for key in sys.argv[2].split(".")]; print(value)' "${LOCK}" "$1"
}

DESIGN_CONFIG="$(read_lock aes_nangate45_smoke.design_config)"
INPUT_STAGE="$(read_lock aes_nangate45_smoke.input_stage)"

if [[ "${MODE}" == "check-only" ]]; then
  FLOW_VARIANT="${FLOW_VARIANT:-$(read_lock aes_nangate45_smoke.reference_flow_variant)}"
  RUN_TAG="${RUN_TAG:-$(read_lock aes_nangate45_smoke.reference_run_tag)}"
else
  stamp="$(dpl_ae_timestamp)"
  FLOW_VARIANT="${FLOW_VARIANT:-ae_smoke_aes_n45_${MODE}_${stamp}}"
  RUN_TAG="${RUN_TAG:-ae_smoke_${MODE}_openroad_dpl_flow}"
fi

FLOW_HOME="${ORFS_ROOT}/flow"
INPUT_ODB="${FLOW_HOME}/results/nangate45/aes/${FLOW_VARIANT}/${INPUT_STAGE}.odb"
METRICS="${FLOW_HOME}/reports/nangate45/aes/${FLOW_VARIANT}/dpl_evolve_baseline/${RUN_TAG}/metrics.json"

if [[ "${MODE}" == "check-only" && ( ! -f "${INPUT_ODB}" || ! -f "${METRICS}" ) ]]; then
  echo "[SKIP] Optional prepared AES smoke result is not available."
  echo "       Expected input:   ${INPUT_ODB}"
  echo "       Expected metrics: ${METRICS}"
  echo "       Run 'make smoke' to create and validate a fresh timestamped result."
  exit 0
fi

echo "=============================================="
echo " DPLEvolve AE — AES Smoke Test"
echo "=============================================="
echo ""
dpl_ae_info "Mode:     ${MODE}"
dpl_ae_info "Variant:  ${FLOW_VARIANT}"
dpl_ae_info "Run tag:  ${RUN_TAG}"
dpl_ae_info "Threads:  ${THREADS}"
dpl_ae_info "Config:   ${DESIGN_CONFIG}"
echo ""

if [[ "${MODE}" != "check-only" ]]; then
  if [[ -e "${METRICS}" || -d "$(dirname "${METRICS}")" ]]; then
    dpl_ae_error "Refusing to overwrite existing smoke run: $(dirname "${METRICS}")"
    exit 1
  fi

  dpl_ae_info "Running environment check..."
  bash "${AE_ROOT}/scripts/human/check_environment.sh"
  echo ""

  dpl_ae_info "Generating AES input snapshot..."
  make -C "${FLOW_HOME}" --no-print-directory \
    DESIGN_CONFIG="${DESIGN_CONFIG}" \
    FLOW_VARIANT="${FLOW_VARIANT}" \
    NUM_CORES="${THREADS}" \
    OPENROAD_EXE="${OPENROAD_EXE}" \
    YOSYS_EXE="${YOSYS_EXE}" \
    check-openroad check-yosys "${INPUT_STAGE}"
  dpl_ae_ok "Snapshot generated: ${INPUT_ODB}"
  echo ""

  dpl_ae_info "Running native OpenROAD detailed-placement baseline..."
  "${DPL_EVOLVE_AGENT_ROOT}/baseline/run_baseline.sh" \
    --line openroad_dpl_flow \
    --design-config "${DESIGN_CONFIG}" \
    --input-stage "${INPUT_STAGE}" \
    --flow-variant "${FLOW_VARIANT}" \
    --run-tag "${RUN_TAG}" \
    --threads "${THREADS}" \
    --openroad-binary "${OPENROAD_EXE}"
  dpl_ae_ok "Baseline run complete"
  echo ""
fi

dpl_ae_info "Validating AES smoke artifacts..."
"${DPL_EVOLVE_PYTHON}" "${DPL_EVOLVE_AGENT_ROOT}/scripts/ae/validate_aes_smoke.py" \
  --lock "${LOCK}" \
  --input-odb "${INPUT_ODB}" \
  --metrics "${METRICS}"

echo ""
dpl_ae_ok "=============================================="
dpl_ae_ok " AES smoke test PASSED"
dpl_ae_ok "=============================================="
