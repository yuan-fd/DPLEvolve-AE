#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AGENT_ROOT="${DPL_EVOLVE_AGENT_ROOT:-$(cd "${SCRIPT_DIR}/../.." && pwd)}"
AGENT_ROOT="$(realpath -m "${AGENT_ROOT}")"
if [[ -f "${AGENT_ROOT}/env.sh" ]]; then
  # shellcheck source=/dev/null
  source "${AGENT_ROOT}/env.sh"
fi
STATE_ROOT="${DPL_EVOLVE_STATE_ROOT:-$(realpath -m "${AGENT_ROOT}/../dpl_evolve_state")}"
if [[ -f "${STATE_ROOT}/ae/environment.sh" ]]; then
  # shellcheck source=/dev/null
  source "${STATE_ROOT}/ae/environment.sh"
fi
source "${AGENT_ROOT}/scripts/runtime_env.sh"
dpl_init_runtime "run_aes_smoke.sh"

MODE=""
FLOW_VARIANT=""
RUN_TAG=""
THREADS="8"
LOCK="${AGENT_ROOT}/metadata/ae_reproduction_lock.json"

usage() {
  cat <<'EOF'
Usage: run_aes_smoke.sh (--check-only | --run | --rebuild) [options]

Modes:
  --check-only       Validate an existing AES snapshot and baseline result.
  --run              Create a new timestamped variant, generate the snapshot,
                     and run the native OpenROAD detailed-placement baseline.
  --rebuild          Same isolated behavior as --run, with a distinct rebuild
                     tag. Existing results are never deleted or overwritten.

Options:
  --flow-variant ID  Existing variant for --check-only. For run modes, a new
                     variant name; it must not already contain the run tag.
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
    --flow-variant) FLOW_VARIANT="$2"; shift 2 ;;
    --run-tag) RUN_TAG="$2"; shift 2 ;;
    --threads) THREADS="$2"; shift 2 ;;
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

read_lock() {
  "${DPL_EVOLVE_PYTHON}" -c 'import json,sys; data=json.load(open(sys.argv[1], encoding="utf-8")); value=data; [value := value[key] for key in sys.argv[2].split(".")]; print(value)' "${LOCK}" "$1"
}

DESIGN_CONFIG="$(read_lock aes_nangate45_smoke.design_config)"
INPUT_STAGE="$(read_lock aes_nangate45_smoke.input_stage)"
if [[ "${MODE}" == "check-only" ]]; then
  FLOW_VARIANT="${FLOW_VARIANT:-$(read_lock aes_nangate45_smoke.reference_flow_variant)}"
  RUN_TAG="${RUN_TAG:-$(read_lock aes_nangate45_smoke.reference_run_tag)}"
else
  stamp="$(date +%Y%m%d_%H%M%S)"
  FLOW_VARIANT="${FLOW_VARIANT:-ae_smoke_aes_n45_${MODE}_${stamp}}"
  RUN_TAG="${RUN_TAG:-ae_smoke_${MODE}_openroad_dpl_flow}"
fi

FLOW_HOME="${ORFS_ROOT}/flow"
INPUT_ODB="${FLOW_HOME}/results/nangate45/aes/${FLOW_VARIANT}/${INPUT_STAGE}.odb"
METRICS="${FLOW_HOME}/reports/nangate45/aes/${FLOW_VARIANT}/dpl_evolve_baseline/${RUN_TAG}/metrics.json"

if [[ "${MODE}" != "check-only" ]]; then
  if [[ -e "${METRICS}" || -d "$(dirname "${METRICS}")" ]]; then
    echo "[ERROR] Refusing to overwrite an existing smoke run: $(dirname "${METRICS}")" >&2
    exit 1
  fi
  "${SCRIPT_DIR}/check_environment.sh"
  echo "[INFO] Generating AES input snapshot in new variant ${FLOW_VARIANT}."
  make -C "${FLOW_HOME}" --no-print-directory \
    DESIGN_CONFIG="${DESIGN_CONFIG}" \
    FLOW_VARIANT="${FLOW_VARIANT}" \
    NUM_CORES="${THREADS}" \
    OPENROAD_EXE="${OPENROAD_EXE}" \
    YOSYS_EXE="${YOSYS_EXE}" \
    check-openroad check-yosys "${INPUT_STAGE}"
  echo "[INFO] Running the native OpenROAD detailed-placement baseline."
  "${AGENT_ROOT}/baseline/run_baseline.sh" \
    --line openroad_dpl_flow \
    --design-config "${DESIGN_CONFIG}" \
    --input-stage "${INPUT_STAGE}" \
    --flow-variant "${FLOW_VARIANT}" \
    --run-tag "${RUN_TAG}" \
    --threads "${THREADS}" \
    --openroad-binary "${OPENROAD_EXE}"
fi

echo "[INFO] Validating AES smoke artifacts."
"${DPL_EVOLVE_PYTHON}" "${SCRIPT_DIR}/validate_aes_smoke.py" \
  --lock "${LOCK}" \
  --input-odb "${INPUT_ODB}" \
  --metrics "${METRICS}"
echo "[OK] AES smoke validation passed for ${FLOW_VARIANT}/${RUN_TAG}."
