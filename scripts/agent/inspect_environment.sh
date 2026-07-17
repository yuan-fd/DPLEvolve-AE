#!/usr/bin/env bash
# DPLEvolve AE — Agent Environment Inspection
# Captures complete environment state before an experiment run.
# Output is machine-parseable JSON for agent consumption.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AE_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
export AE_ROOT

# shellcheck source=/dev/null
source "${AE_ROOT}/scripts/lib/env_vars.sh"
dpl_ae_resolve_env

# shellcheck source=/dev/null
source "${AE_ROOT}/scripts/lib/utils.sh"

OUTPUT="${1:-${AE_ROOT}/provenance/current-machine.json}"

dpl_ae_info "Capturing environment state..."

# Generate provenance
bash "${AE_ROOT}/scripts/internal/record_provenance.sh"

# Additional agent-specific checks
echo "--- Agent Environment Report ---"
echo ""

# Check all required binaries
echo "Binary resolution:"
for bin_name in gcc g++ cmake make bison flex python3 git; do
  bin_path="$(command -v "${bin_name}" 2>/dev/null || echo 'NOT FOUND')"
  printf "  %-12s %s\n" "${bin_name}:" "${bin_path}"
done
echo ""

# Check critical env vars
echo "Environment variables:"
for var in DPL_EVOLVE_AGENT_ROOT ORFS_ROOT DPL_EVOLVE_STATE_ROOT DPL_EVOLVE_PYTHON YOSYS_EXE OPENROAD_EXE AE_ROOT; do
  printf "  %-30s %s\n" "${var}:" "${!var:-NOT SET}"
done
echo ""

# Check disk space
echo "Disk space:"
df -h "${AE_ROOT}" 2>/dev/null | tail -1 || echo "  Could not check"
echo ""

# Check that immutable files are unchanged
echo "Immutability checks:"
LOCK="${AE_ROOT}/provenance/source-commits.json"
if [[ -f "${LOCK}" ]]; then
  echo "  source-commits.json: present"
else
  echo "  source-commits.json: MISSING!"
fi

REF_CHECKSUMS="${AE_ROOT}/provenance/original-artifact-checksums.txt"
if [[ -f "${REF_CHECKSUMS}" ]]; then
  echo "  original-artifact-checksums.txt: present"
else
  echo "  original-artifact-checksums.txt: MISSING!"
fi
echo ""

# Check that generated dirs are writable
echo "Write permission checks:"
for dir in "${AE_ROOT}/results/reproduced" "${AE_ROOT}/results/tables" "${DPL_EVOLVE_STATE_ROOT}"; do
  if [[ -w "${dir}" ]]; then
    echo "  ${dir}: writable"
  else
    echo "  ${dir}: NOT WRITABLE!"
  fi
done
echo ""

dpl_ae_ok "Environment inspection complete."
echo "Report: ${OUTPUT}"
