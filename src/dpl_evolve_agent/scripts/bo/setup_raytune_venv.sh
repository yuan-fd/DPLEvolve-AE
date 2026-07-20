#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AGENT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3.11}"
VENV_DIR="${RAYTUNE_VENV_DIR:-${AGENT_ROOT}/.venv_raytune}"
REQ_FILE="${AGENT_ROOT}/configs/raytune_requirements.txt"

if ! command -v "${PYTHON_BIN}" >/dev/null 2>&1; then
  echo "[ERROR] Python interpreter not found: ${PYTHON_BIN}" >&2
  echo "        Set PYTHON_BIN=python3.11 or another Ray-compatible Python." >&2
  exit 1
fi

"${PYTHON_BIN}" -m venv "${VENV_DIR}"
"${VENV_DIR}/bin/python" -m pip install --upgrade pip setuptools wheel
LOCAL_RAY_WHEEL="${RAYTUNE_LOCAL_RAY_WHEEL:-${AGENT_ROOT}/.dpl_evolve_state/cache/ray_wheels/ray-2.31.0-cp311-cp311-manylinux2014_x86_64.whl}"
if [[ -f "${LOCAL_RAY_WHEEL}" ]] && "${VENV_DIR}/bin/python" - <<'PY'
import sys
raise SystemExit(0 if sys.version_info[:2] == (3, 11) else 1)
PY
then
  "${VENV_DIR}/bin/python" -m pip install --timeout 180 --retries 10 "${LOCAL_RAY_WHEEL}"
fi
"${VENV_DIR}/bin/python" -m pip install --timeout 180 --retries 10 -r "${REQ_FILE}"
"${VENV_DIR}/bin/python" - <<'PY'
import ray
from ray import tune
import optuna
print("ray", ray.__version__)
print("ray.tune OK")
print("optuna", optuna.__version__)
PY

echo "[INFO] Ray Tune venv ready: ${VENV_DIR}"
