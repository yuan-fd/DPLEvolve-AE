#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
status=0
bash "${SCRIPT_DIR}/reproduce_table5.sh" --check-inputs || status=1
bash "${SCRIPT_DIR}/reproduce_table6.sh" --check-paper-data || status=1
if [[ "${status}" -eq 0 ]]; then
  echo "[PASS] exact Table 5/6 replay data is installed"
else
  echo "[BLOCKED] exact Table 5/6 replay data is incomplete" >&2
fi
exit "${status}"
