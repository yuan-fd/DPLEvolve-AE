#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
status=0
bash "${SCRIPT_DIR}/reproduce_table5.sh" --check-paper-data || status=1
bash "${SCRIPT_DIR}/reproduce_table6.sh" --check-paper-data || status=1
if [[ "${status}" -eq 0 ]]; then
  echo "[PASS] Table 5 snapshots and the Table 6 replay package are installed"
else
  echo "[BLOCKED] a snapshot or external paper-data check failed; see the specific message above" >&2
fi
exit "${status}"
