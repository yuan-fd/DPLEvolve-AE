#!/usr/bin/env bash
set -euo pipefail

EXPERIMENT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHON_BIN="${DPL_EVOLVE_PYTHON:-python3}"

"${PYTHON_BIN}" "${EXPERIMENT_ROOT}/verify.py" \
  --input "${EXPERIMENT_ROOT}/inputs" \
  --output "${EXPERIMENT_ROOT}/output" \
  --claims "${EXPERIMENT_ROOT}/expected/paper_claims.json" \
  --paper-table "${EXPERIMENT_ROOT}/expected/table4.json" \
  --strict-paper-claims

bash "${EXPERIMENT_ROOT}/selected-programs/run.sh" --sources-only

echo "[PASS] Table 4 archived records and selected-source integrity checks passed"
