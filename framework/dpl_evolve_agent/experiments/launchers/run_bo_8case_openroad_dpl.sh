#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AGENT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${AGENT_ROOT}/scripts/runtime_env.sh"
dpl_init_runtime "run_bo_8case_openroad_dpl.sh"

PYTHON_BIN="${RAYTUNE_PYTHON:-${AGENT_ROOT}/.venv_raytune/bin/python}"
SPACE="${BO_SPACE:-${AGENT_ROOT}/configs/bo_search_spaces/openroad_dpl_native.yaml}"
TRIALS="${BO_TRIALS:-100}"
MAX_CONCURRENT_TRIALS="${BO_MAX_CONCURRENT_TRIALS:-4}"
THREADS="${BO_THREADS_PER_TRIAL:-10}"
RAY_CPUS="${BO_RAY_CPUS:-${MAX_CONCURRENT_TRIALS}}"
SEED="${BO_SEED:-1}"
STARTUP_TRIALS="${BO_STARTUP_TRIALS:-}"
TPE_CANDIDATES="${BO_TPE_CANDIDATES:-64}"
ANCHOR_STRATEGY="${BO_ANCHOR_STRATEGY:-mechanism}"
RUN_PREFIX="${BO_RUN_PREFIX:-openroad_dpl_native_8case_bo}"
LEGALIZE_TIMEOUT="${BO_LEGALIZE_TIMEOUT_SECONDS:-}"

CASES=(
  aes_asap7
  aes_nangate45
  ariane133_dense_nangate45
  ibex_nangate45
  jpeg_asap7
  swerv_wrapper_asap7
  jpeg_util90_nangate45
  aes_dense_nangate45
)

if [[ ! -x "${PYTHON_BIN}" ]]; then
  echo "[ERROR] Ray Tune Python is not executable: ${PYTHON_BIN}" >&2
  echo "        Run ${AGENT_ROOT}/scripts/bo/setup_raytune_venv.sh first." >&2
  exit 1
fi

for case_id in "${CASES[@]}"; do
  run_id="${RUN_PREFIX}_${case_id}"
  cmd=(
    "${PYTHON_BIN}" "${AGENT_ROOT}/scripts/bo/bo_tune_case.py"
    --case "${case_id}"
    --space "${SPACE}"
    --run-id "${run_id}"
    --trials "${TRIALS}"
    --max-concurrent-trials "${MAX_CONCURRENT_TRIALS}"
    --threads "${THREADS}"
    --ray-cpus "${RAY_CPUS}"
    --seed "${SEED}"
    --anchor-strategy "${ANCHOR_STRATEGY}"
    --tpe-candidates "${TPE_CANDIDATES}"
    --overwrite
  )
  if [[ -n "${STARTUP_TRIALS}" ]]; then
    cmd+=(--startup-trials "${STARTUP_TRIALS}")
  fi
  if [[ -n "${LEGALIZE_TIMEOUT}" ]]; then
    cmd+=(--legalize-timeout-seconds "${LEGALIZE_TIMEOUT}")
  fi
  echo "[INFO] BO case=${case_id} run_id=${run_id}"
  "${cmd[@]}"
done

echo "[INFO] BO 8-case sweep complete."
