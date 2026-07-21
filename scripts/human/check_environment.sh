#!/usr/bin/env bash
# DPLEvolve AE — Environment Check (Human Entry Point)
# Validates that the environment is ready for artifact evaluation.
# Usage: make check   OR   ./scripts/human/check_environment.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AE_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
export AE_ROOT

# shellcheck source=/dev/null
source "${AE_ROOT}/scripts/shared/env_vars.sh"
dpl_ae_resolve_env

# shellcheck source=/dev/null
source "${AE_ROOT}/scripts/shared/utils.sh"

echo "=============================================="
echo " DPLEvolve Artifact Evaluation — Environment"
echo "=============================================="
echo ""

# --- Basic system check ---
echo "--- System ---"
echo "  Hostname: $(hostname)"
echo "  OS:       $(cat /etc/os-release 2>/dev/null | head -1 | cut -d'"' -f2 || echo unknown)"
echo "  Kernel:   $(uname -r)"
echo "  Arch:     $(uname -m)"
echo "  Cores:    $(nproc)"
echo "  Disk:     $(df -BG "${AE_ROOT}" 2>/dev/null | awk 'NR==2{print $4}' || echo unknown) available"
echo ""

# --- Toolchain ---
echo "--- Toolchain ---"
for cmd in gcc cmake make g++; do
  if command -v "${cmd}" >/dev/null 2>&1; then
    echo "  ${cmd}: $(command -v "${cmd}")"
  else
    dpl_ae_warn "${cmd}: NOT FOUND"
  fi
done
echo ""

# --- Python ---
echo "--- Python ---"
echo "  Path:    ${DPL_EVOLVE_PYTHON}"
"${DPL_EVOLVE_PYTHON}" --version 2>&1 || dpl_ae_warn "Python version check failed"
if "${DPL_EVOLVE_PYTHON}" -c "import yaml" 2>/dev/null; then
  echo "  PyYAML:  $("${DPL_EVOLVE_PYTHON}" -c "import yaml; print(yaml.__version__)" 2>/dev/null || echo 'imported')"
else
  dpl_ae_warn "PyYAML: NOT INSTALLED"
fi
echo ""

# --- Paths ---
echo "--- Project Paths ---"
echo "  AE_ROOT:                  ${AE_ROOT}"
echo "  DPL_EVOLVE_AGENT_ROOT:    ${DPL_EVOLVE_AGENT_ROOT:-NOT SET}"
echo "  ORFS_ROOT:                ${ORFS_ROOT:-NOT SET}"
echo "  DPL_EVOLVE_STATE_ROOT:    ${DPL_EVOLVE_STATE_ROOT:-NOT SET}"
echo ""

# --- Repository commits ---
echo "--- Repository Commits ---"
LOCK="${AE_ROOT}/provenance/source-commits.json"
if [[ -f "${LOCK}" ]]; then
  AGENT_COMMIT="$(dpl_ae_json_get "${LOCK}" "repositories.dpl_evolve_agent.base_commit" 2>/dev/null || echo '')"
  ORFS_COMMIT="$(dpl_ae_json_get "${LOCK}" "repositories.openroad_flow_scripts.prepared_commit" 2>/dev/null || echo '')"
  OR_COMMIT="$(dpl_ae_json_get "${LOCK}" "repositories.openroad.prepared_commit" 2>/dev/null || echo '')"

  if [[ -n "${AGENT_COMMIT}" && -d "${DPL_EVOLVE_AGENT_ROOT}/.git" ]]; then
    ac="$(git -C "${DPL_EVOLVE_AGENT_ROOT}" rev-parse HEAD)"
    if [[ "${ac}" == "${AGENT_COMMIT}" ]]; then
      dpl_ae_ok "dpl_evolve_agent: ${ac:0:8}"
    else
      dpl_ae_warn "dpl_evolve_agent: expected ${AGENT_COMMIT:0:8}, got ${ac:0:8}"
    fi
  fi

  if [[ -n "${ORFS_COMMIT}" && -d "${ORFS_ROOT}/.git" ]]; then
    oc="$(git -C "${ORFS_ROOT}" rev-parse HEAD)"
    if [[ "${oc}" == "${ORFS_COMMIT}" ]]; then
      dpl_ae_ok "ORFS: ${oc:0:8}"
    else
      dpl_ae_warn "ORFS: expected ${ORFS_COMMIT:0:8}, got ${oc:0:8}"
    fi
  fi

  if [[ -n "${OR_COMMIT}" ]] && \
     git -C "${ORFS_ROOT}/tools/OpenROAD" rev-parse --git-dir >/dev/null 2>&1; then
    orc="$(git -C "${ORFS_ROOT}/tools/OpenROAD" rev-parse HEAD)"
    if [[ "${orc}" == "${OR_COMMIT}" ]]; then
      dpl_ae_ok "OpenROAD: ${orc:0:8}"
    else
      dpl_ae_warn "OpenROAD: expected ${OR_COMMIT:0:8}, got ${orc:0:8}"
    fi
  fi
fi
echo ""

# --- Yosys ---
echo "--- Yosys ---"
YOSYS_BIN="${YOSYS_EXE:-${DPL_EVOLVE_STATE_ROOT}/yosys/8449dd470/bin/yosys}"
if [[ -x "${YOSYS_BIN}" ]]; then
  echo "  Path:    ${YOSYS_BIN}"
  echo "  Version: $("${YOSYS_BIN}" -V 2>&1 | head -1 || echo 'unknown')"
  echo "  SHA-256: $(sha256sum "${YOSYS_BIN}" | awk '{print $1}')"
else
  dpl_ae_warn "Yosys binary not found at: ${YOSYS_BIN}"
  dpl_ae_warn "Run 'make setup' to build it."
fi
echo ""

# --- OpenROAD ---
echo "--- OpenROAD ---"
OR_BIN="${OPENROAD_EXE:-${DPL_EVOLVE_STATE_ROOT}/openroad_core/d5ff63a/install/OpenROAD/bin/openroad}"
if [[ -x "${OR_BIN}" ]]; then
  echo "  Path:    ${OR_BIN}"
  echo "  Version: $("${OR_BIN}" -version 2>&1 | head -1 || echo 'unknown')"
  echo "  SHA-256: $(sha256sum "${OR_BIN}" | awk '{print $1}')"
else
  dpl_ae_warn "OpenROAD binary not found at: ${OR_BIN}"
  dpl_ae_warn "Run 'make setup' to build it."
fi
echo ""

# --- Dynamic libraries ---
echo "--- Shared Library Resolution ---"
if [[ -x "${OR_BIN}" ]]; then
  if command -v ldd >/dev/null 2>&1; then
    missing="$(ldd "${OR_BIN}" 2>&1 | grep 'not found' || true)"
    if [[ -n "${missing}" ]]; then
      dpl_ae_warn "Missing shared libraries:"
      echo "${missing}"
    else
      dpl_ae_ok "All shared libraries resolved"
    fi
  fi
fi
echo ""

echo "=============================================="
echo " Environment check complete."
echo " Run 'make setup' if any warnings above."
echo " Run 'make smoke' to validate with AES test."
echo "=============================================="
