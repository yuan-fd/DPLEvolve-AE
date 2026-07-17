#!/usr/bin/env bash
# DPLEvolve AE — AES Smoke Test (Human Entry Point)
# Runs the minimal AES smoke test using the pinned environment.
# Usage: make smoke   OR   ./scripts/human/smoke_test.sh [--run|--check-only|--rebuild]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AE_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
export AE_ROOT

# shellcheck source=/dev/null
source "${AE_ROOT}/scripts/lib/env_vars.sh"
dpl_ae_resolve_env

# shellcheck source=/dev/null
source "${AE_ROOT}/scripts/lib/utils.sh"

MODE=""
FLOW_VARIANT=""
RUN_TAG=""
THREADS="8"

usage() {
  cat <<'EOF'
Usage: smoke_test.sh (--check-only | --run | --rebuild) [options]

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
      if [[ -n "${MODE}" ]]; then dpl_ae_error "Choose exactly one mode." >&2; exit 2; fi
      MODE="${1#--}"
      shift
      ;;
    --flow-variant) FLOW_VARIANT="$2"; shift 2 ;;
    --run-tag) RUN_TAG="$2"; shift 2 ;;
    --threads) THREADS="$2"; shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) dpl_ae_error "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done
if [[ -z "${MODE}" ]]; then
  dpl_ae_error "Choose --check-only, --run, or --rebuild." >&2
  usage >&2
  exit 2
fi
if ! [[ "${THREADS}" =~ ^[1-9][0-9]*$ ]]; then
  dpl_ae_error "--threads must be a positive integer." >&2
  exit 2
fi

LOCK="${DPL_EVOLVE_AGENT_ROOT}/metadata/ae_reproduction_lock.json"
if [[ ! -f "${LOCK}" ]]; then
  dpl_ae_error "Reproduction lock not found: ${LOCK}"
  exit 1
fi

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
  bash "${SCRIPT_DIR}/check_environment.sh"
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
