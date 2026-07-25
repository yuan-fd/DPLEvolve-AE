#!/usr/bin/env bash
# DPLEvolve AE — Environment Setup (Human Entry Point)
# Idempotent setup: builds Yosys, OpenROAD, and Python venv if needed.
# Usage: make build-tools   OR   ./scripts/human/setup.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AE_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
export AE_ROOT

# shellcheck source=/dev/null
source "${AE_ROOT}/scripts/shared/env_vars.sh"
dpl_ae_resolve_env

# shellcheck source=/dev/null
source "${AE_ROOT}/scripts/shared/utils.sh"

JOBS="${JOBS:-8}"
SKIP_YOSYS="${SKIP_YOSYS:-0}"
SKIP_OPENROAD="${SKIP_OPENROAD:-0}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --jobs)
      if [[ $# -lt 2 ]]; then
        dpl_ae_error "--jobs requires a positive integer"
        exit 2
      fi
      JOBS="$2"
      shift 2
      ;;
    --skip-yosys) SKIP_YOSYS=1; shift ;;
    --skip-openroad) SKIP_OPENROAD=1; shift ;;
    *)
      dpl_ae_error "Unknown argument: $1"
      exit 2
      ;;
  esac
done
if ! [[ "${JOBS}" =~ ^[1-9][0-9]*$ ]]; then
  dpl_ae_error "--jobs must be a positive integer"
  exit 2
fi

echo "=============================================="
echo " DPLEvolve AE — Environment Setup"
echo "=============================================="
echo ""
dpl_ae_info "Jobs: ${JOBS}"
dpl_ae_info "AE_ROOT: ${AE_ROOT}"
dpl_ae_info "ORFS_ROOT: ${ORFS_ROOT}"
dpl_ae_info "State root: ${DPL_EVOLVE_STATE_ROOT}"
echo ""

# --- Load server modules ---
if type module >/dev/null 2>&1; then
  module load gcc/default 2>/dev/null || dpl_ae_warn "Could not load gcc/default module"
  module load openroad 2>/dev/null || dpl_ae_warn "Could not load openroad module"
fi

# --- Check prerequisites ---
for cmd in git make python3 gcc g++ cmake; do
  dpl_ae_require_cmd "${cmd}" || exit 1
done
dpl_ae_ok "Prerequisite commands found"
echo ""

# --- Verify repository commits ---
LOCK="${AE_ROOT}/provenance/source-commits.json"
if [[ ! -f "${LOCK}" ]]; then
  dpl_ae_error "Source commit lock not found: ${LOCK}"
  exit 1
fi

AGENT_COMMIT="$(dpl_ae_json_get "${LOCK}" "repositories.dpl_evolve_agent.base_commit")"
ORFS_COMMIT="$(dpl_ae_json_get "${LOCK}" "repositories.openroad_flow_scripts.prepared_commit")"
ORFS_TREE="$(dpl_ae_json_get "${LOCK}" "repositories.openroad_flow_scripts.prepared_tree")"
OR_COMMIT="$(dpl_ae_json_get "${LOCK}" "repositories.openroad.prepared_commit")"
OR_TREE="$(dpl_ae_json_get "${LOCK}" "repositories.openroad.prepared_tree")"
YOSYS_COMMIT="$(dpl_ae_json_get "${LOCK}" "submodules.yosys.commit")"
YOSYS_SLANG_COMMIT="$(dpl_ae_json_get "${LOCK}" "submodules.yosys_slang.commit")"

dpl_ae_info "Verifying repository versions..."
if [[ -d "${DPL_EVOLVE_AGENT_ROOT}/.git" ]]; then
  dpl_ae_check_git_commit "${DPL_EVOLVE_AGENT_ROOT}" "${AGENT_COMMIT}" "dpl_evolve_agent" || {
    dpl_ae_warn "Agent repo commit mismatch. Continuing with the requested checkout..."
  }
elif [[ "$(realpath -m "${DPL_EVOLVE_AGENT_ROOT}")" == "$(realpath -m "${AE_ROOT}/src/dpl_evolve_agent")" \
     && -f "${DPL_EVOLVE_AGENT_ROOT}/metadata/anchors.json" \
     && -f "${DPL_EVOLVE_AGENT_ROOT}/scripts/runtime_env.sh" ]]; then
  dpl_ae_ok "Bundled dpl_evolve_agent snapshot found (recorded source ${AGENT_COMMIT:0:8})"
else
  dpl_ae_error "dpl_evolve_agent source tree is incomplete: ${DPL_EVOLVE_AGENT_ROOT}"
  exit 1
fi
ORFS_ACTUAL_COMMIT="$(git -C "${ORFS_ROOT}" rev-parse HEAD)"
ORFS_ACTUAL_TREE="$(git -C "${ORFS_ROOT}" rev-parse 'HEAD^{tree}')"
if [[ "${ORFS_ACTUAL_COMMIT}" == "${ORFS_COMMIT}" || "${ORFS_ACTUAL_TREE}" == "${ORFS_TREE}" ]]; then
  dpl_ae_ok "ORFS source tree matches the prepared AE revision"
else
  dpl_ae_error "ORFS source mismatch. Run 'make bootstrap' on a clean path."
  dpl_ae_error "Expected commit/tree: ${ORFS_COMMIT} / ${ORFS_TREE}"
  dpl_ae_error "Actual commit/tree:   ${ORFS_ACTUAL_COMMIT} / ${ORFS_ACTUAL_TREE}"
  exit 1
fi

OR_ACTUAL_COMMIT="$(git -C "${ORFS_ROOT}/tools/OpenROAD" rev-parse HEAD)"
OR_ACTUAL_TREE="$(git -C "${ORFS_ROOT}/tools/OpenROAD" rev-parse 'HEAD^{tree}')"
if [[ "${OR_ACTUAL_COMMIT}" == "${OR_COMMIT}" || "${OR_ACTUAL_TREE}" == "${OR_TREE}" ]]; then
  dpl_ae_ok "OpenROAD source tree matches the prepared AE revision"
else
  dpl_ae_error "OpenROAD source mismatch. Run 'make bootstrap' on a clean path."
  dpl_ae_error "Expected commit/tree: ${OR_COMMIT} / ${OR_TREE}"
  dpl_ae_error "Actual commit/tree:   ${OR_ACTUAL_COMMIT} / ${OR_ACTUAL_TREE}"
  exit 1
fi
OPENROAD_ANCHOR_ID="$(git -C "${ORFS_ROOT}/tools/OpenROAD" rev-parse --short HEAD)"
echo ""

# --- Initialize ORFS synthesis submodules ---
dpl_ae_info "Initializing ORFS Yosys and yosys-slang submodules..."
git -C "${ORFS_ROOT}" submodule update --init --recursive tools/yosys tools/yosys-slang
YOSYS_ACTUAL="$(git -C "${ORFS_ROOT}/tools/yosys" rev-parse HEAD)"
if [[ "${YOSYS_ACTUAL}" != "${YOSYS_COMMIT}" ]]; then
  dpl_ae_error "Yosys submodule commit mismatch!"
  dpl_ae_error "  expected: ${YOSYS_COMMIT}"
  dpl_ae_error "  actual:   ${YOSYS_ACTUAL}"
  dpl_ae_error "Check that ORFS is at the correct prepared commit."
  exit 1
fi
dpl_ae_ok "Yosys submodule: ${YOSYS_COMMIT:0:8}"
YOSYS_SLANG_ACTUAL="$(git -C "${ORFS_ROOT}/tools/yosys-slang" rev-parse HEAD)"
if [[ "${YOSYS_SLANG_ACTUAL}" != "${YOSYS_SLANG_COMMIT}" ]]; then
  dpl_ae_error "yosys-slang submodule commit mismatch!"
  dpl_ae_error "  expected: ${YOSYS_SLANG_COMMIT}"
  dpl_ae_error "  actual:   ${YOSYS_SLANG_ACTUAL}"
  exit 1
fi
dpl_ae_ok "yosys-slang submodule: ${YOSYS_SLANG_COMMIT:0:8}"
echo ""

# --- Python virtual environment ---
VENV_ROOT="${AE_ROOT}/../.venvs/dplevolve"
if [[ ! -x "${VENV_ROOT}/bin/python" ]]; then
  dpl_ae_info "Creating Python virtual environment: ${VENV_ROOT}"
  python3 -m venv "${VENV_ROOT}"
fi

YAML_VERSION="$(dpl_ae_json_get "${LOCK}" "python.packages.PyYAML")"
if ! "${VENV_ROOT}/bin/python" -c "import importlib.metadata; importlib.metadata.version('PyYAML')" 2>/dev/null | grep -q "${YAML_VERSION}"; then
  dpl_ae_info "Installing PyYAML ${YAML_VERSION}..."
  "${VENV_ROOT}/bin/python" -m pip install "PyYAML==${YAML_VERSION}" --quiet
fi
dpl_ae_ok "Python venv ready: ${VENV_ROOT}/bin/python"
export DPL_EVOLVE_PYTHON="${VENV_ROOT}/bin/python"
echo ""

# --- Build Yosys ---
YOSYS_PREFIX="${DPL_EVOLVE_STATE_ROOT}/yosys/8449dd470"
YOSYS_BIN="${YOSYS_PREFIX}/bin/yosys"
if [[ -x "${YOSYS_BIN}" ]]; then
  dpl_ae_ok "Yosys binary exists: ${YOSYS_BIN}"
elif [[ "${SKIP_YOSYS}" -eq 1 ]]; then
  dpl_ae_error "Yosys binary missing and --skip-yosys requested"
  exit 1
else
  dpl_ae_info "Building Yosys (this may take several minutes)..."
  make -C "${ORFS_ROOT}/tools/yosys" -j"${JOBS}" PREFIX="${YOSYS_PREFIX}"
  make -C "${ORFS_ROOT}/tools/yosys" install PREFIX="${YOSYS_PREFIX}"
  if [[ ! -x "${YOSYS_BIN}" ]]; then
    dpl_ae_error "Yosys build failed — binary not found at ${YOSYS_BIN}"
    exit 1
  fi
  dpl_ae_ok "Yosys built: ${YOSYS_BIN}"
fi

SLANG_PLUGIN="${YOSYS_PREFIX}/share/yosys/plugins/slang.so"
if [[ -f "${SLANG_PLUGIN}" ]]; then
  dpl_ae_ok "yosys-slang plugin exists: ${SLANG_PLUGIN}"
elif [[ "${SKIP_YOSYS}" -eq 1 ]]; then
  dpl_ae_error "yosys-slang plugin missing and --skip-yosys requested: ${SLANG_PLUGIN}"
  exit 1
else
  dpl_ae_info "Building yosys-slang (required by SystemVerilog paper inputs)..."
  make -C "${ORFS_ROOT}/tools/yosys-slang" install -j"${JOBS}" \
    YOSYS_PREFIX="${YOSYS_PREFIX}/bin/" \
    CMAKE_FLAGS="-DYOSYS_SLANG_REVISION=unknown -DSLANG_REVISION=unknown"
  if [[ ! -f "${SLANG_PLUGIN}" ]]; then
    dpl_ae_error "yosys-slang build finished but the plugin was not found: ${SLANG_PLUGIN}"
    exit 1
  fi
  dpl_ae_ok "yosys-slang built: ${SLANG_PLUGIN}"
fi
echo ""

# --- Build OpenROAD ---
OR_PREFIX="${DPL_EVOLVE_STATE_ROOT}/openroad_core/${OPENROAD_ANCHOR_ID}/install/OpenROAD"
OR_BIN="${OR_PREFIX}/bin/openroad"
if [[ -x "${OR_BIN}" ]]; then
  dpl_ae_ok "OpenROAD binary exists: ${OR_BIN}"
elif [[ "${SKIP_OPENROAD}" -eq 1 ]]; then
  dpl_ae_error "OpenROAD binary missing and --skip-openroad requested"
  exit 1
else
  dpl_ae_info "Building OpenROAD (this may take 15-30 minutes)..."
  if [[ -f "${DPL_EVOLVE_AGENT_ROOT}/scripts/workspace/build_openroad_core.sh" ]]; then
    bash "${DPL_EVOLVE_AGENT_ROOT}/scripts/workspace/build_openroad_core.sh" --threads "${JOBS}"
  else
    dpl_ae_error "OpenROAD build script not found. Build manually and set OPENROAD_EXE."
    exit 1
  fi
  if [[ ! -x "${OR_BIN}" ]]; then
    dpl_ae_error "OpenROAD build finished but the binary was not found: ${OR_BIN}"
    exit 1
  fi
  dpl_ae_ok "OpenROAD built: ${OR_BIN}"
fi
echo ""

# --- Write machine-local environment ---
mkdir -p "${DPL_EVOLVE_STATE_ROOT}/ae"
ENV_FILE="${DPL_EVOLVE_STATE_ROOT}/ae/environment.sh"
cat > "${ENV_FILE}" <<ENVEOF
#!/usr/bin/env bash
# DPLEvolve AE — Machine-Local Environment
# Auto-generated by setup.sh on $(date -Iseconds)

if type module >/dev/null 2>&1; then
  module load gcc/default
  module load openroad
fi

export DPL_EVOLVE_AGENT_ROOT="${DPL_EVOLVE_AGENT_ROOT}"
export ORFS_ROOT="${ORFS_ROOT}"
export DPL_EVOLVE_STATE_ROOT="${DPL_EVOLVE_STATE_ROOT}"
export DPL_EVOLVE_PYTHON="${DPL_EVOLVE_PYTHON}"
export YOSYS_EXE="${YOSYS_BIN}"
export OPENROAD_EXE="${OR_BIN}"
export AE_ROOT="${AE_ROOT}"
ENVEOF
chmod +x "${ENV_FILE}"
dpl_ae_ok "Machine-local environment: ${ENV_FILE}"
echo ""

# --- Final check ---
dpl_ae_info "Setup complete. Running environment check..."
bash "${SCRIPT_DIR}/check_environment.sh"
echo ""
dpl_ae_info "=============================================="
dpl_ae_info " Setup complete!"
dpl_ae_info " Next step: make prepare-paper-inputs"
dpl_ae_info "=============================================="
