#!/usr/bin/env bash
# Non-mutating preflight for DPLEvolve artifact reviewers.
# This script intentionally does not source env_vars.sh: it must work before
# ORFS, the state directory, or any EDA binary exists.

set -uo pipefail

STRICT_SMOKE=0
case "${1:-}" in
  "") ;;
  --strict-smoke) STRICT_SMOKE=1 ;;
  -h|--help)
    cat <<'EOF'
Usage: bash scripts/human/doctor.sh [--strict-smoke]

Checks the host without installing or modifying anything.

  default         Repository/web prerequisites are required. EDA experiment
                  preparation is reported separately.
  --strict-smoke  Compatibility name: also require the prepared ORFS workspace
                  and pinned EDA binaries.
EOF
    exit 0
    ;;
  *)
    echo "[ERROR] Unknown argument: $1" >&2
    exit 2
    ;;
esac

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AE_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
ORFS_ROOT="${ORFS_ROOT:-$(realpath -m "${AE_ROOT}/../OpenROAD-flow-scripts")}"
STATE_ROOT="${DPL_EVOLVE_STATE_ROOT:-$(realpath -m "${AE_ROOT}/../dpl_evolve_state")}"

PASS_COUNT=0
WARN_COUNT=0
FAIL_COUNT=0
EVIDENCE_ERRORS=0
WEB_ERRORS=0
SMOKE_ERRORS=0
BUILD_ERRORS=0
MISSING_EVIDENCE=()
MISSING_WEB=()
MISSING_SMOKE=()

ok() {
  PASS_COUNT=$((PASS_COUNT + 1))
  printf '[OK]    %s\n' "$*"
}

warn() {
  WARN_COUNT=$((WARN_COUNT + 1))
  printf '[WARN]  %s\n' "$*" >&2
}

fail_evidence() {
  FAIL_COUNT=$((FAIL_COUNT + 1))
  EVIDENCE_ERRORS=$((EVIDENCE_ERRORS + 1))
  printf '[ERROR] %s\n' "$*" >&2
}

fail_web() {
  FAIL_COUNT=$((FAIL_COUNT + 1))
  WEB_ERRORS=$((WEB_ERRORS + 1))
  printf '[ERROR] %s\n' "$*" >&2
}

missing_build() {
  BUILD_ERRORS=$((BUILD_ERRORS + 1))
  warn "$*"
}

missing_smoke() {
  SMOKE_ERRORS=$((SMOKE_ERRORS + 1))
  warn "$*"
}

version_ge() {
  local actual="$1"
  local minimum="$2"
  [[ "$(printf '%s\n%s\n' "${minimum}" "${actual}" | sort -V | head -n 1)" == "${minimum}" ]]
}

first_version() {
  grep -Eo '[0-9]+([.][0-9]+)+' | head -n 1
}

command_path() {
  command -v "$1" 2>/dev/null || true
}

resolved_tool_path() {
  local command="$1"
  local path
  path="$(command_path "${command}")"
  if [[ -n "${path}" ]]; then
    printf '%s\n' "${path}"
    return 0
  fi

  # A prepared OpenROAD build can intentionally use dependencies outside PATH.
  # CMakeCache is authoritative for the executable that a rebuild will reuse.
  if [[ "${command}" == "swig" ]]; then
    local cache cached_path
    for cache in "${STATE_ROOT}"/openroad_core/*/build/CMakeCache.txt; do
      [[ -f "${cache}" ]] || continue
      cached_path="$(sed -n 's/^SWIG_EXECUTABLE:FILEPATH=//p' "${cache}" | head -n 1)"
      if [[ -x "${cached_path}" ]]; then
        printf '%s\n' "${cached_path}"
        return 0
      fi
    done
  fi
  return 1
}

printf '%s\n' '============================================================'
printf '%s\n' ' DPLEvolve Artifact Reviewer Doctor'
printf '%s\n' ' Read-only: no packages or files will be changed.'
printf '%s\n' '============================================================'
printf '\n'

OS_ID="unknown"
OS_NAME="unknown Linux"
if [[ -r /etc/os-release ]]; then
  # shellcheck source=/dev/null
  source /etc/os-release
  OS_ID="${ID:-unknown}"
  OS_NAME="${PRETTY_NAME:-${NAME:-unknown Linux}}"
fi

printf '%s\n' '--- Host ---'
printf '  OS:           %s\n' "${OS_NAME}"
printf '  Architecture: %s\n' "$(uname -m)"
printf '  Kernel:       %s\n' "$(uname -r)"
printf '  Repository:   %s\n' "${AE_ROOT}"
printf '\n'

if [[ "$(uname -s)" == "Linux" ]]; then
  ok 'Linux host detected'
else
  warn 'Archive audit may work, but fresh paper experiments support Linux only'
  SMOKE_ERRORS=$((SMOKE_ERRORS + 1))
fi

if [[ "$(uname -m)" == "x86_64" ]]; then
  ok 'x86-64 architecture detected'
else
  missing_smoke "Fresh paper experiments are validated only on x86-64 (found $(uname -m))"
fi

printf '\n%s\n' '--- Repository command prerequisites ---'

if [[ -n "$(command_path bash)" ]]; then
  bash_version="${BASH_VERSION%%(*}"
  if version_ge "${bash_version}" "4.0"; then
    ok "Bash ${bash_version} (minimum 4.0)"
  else
    fail_evidence "Bash ${bash_version} is too old; version 4.0 or newer is required"
    MISSING_EVIDENCE+=(bash)
  fi
else
  fail_evidence 'Bash is not available'
  MISSING_EVIDENCE+=(bash)
fi

if [[ -n "$(command_path make)" ]]; then
  make_version="$(make --version 2>/dev/null | head -n 1 | first_version)"
  if [[ -n "${make_version}" ]] && version_ge "${make_version}" "4.0"; then
    ok "GNU Make ${make_version} (minimum 4.0)"
  else
    fail_evidence "GNU Make ${make_version:-unknown} is too old; version 4.0 or newer is required"
    MISSING_EVIDENCE+=(make)
  fi
else
  fail_evidence 'GNU Make is not available'
  MISSING_EVIDENCE+=(make)
fi

PYTHON_BIN="$(command_path python3)"
if [[ -n "${PYTHON_BIN}" ]]; then
  python_version="$(${PYTHON_BIN} -c 'import platform; print(platform.python_version())' 2>/dev/null || true)"
  if [[ -n "${python_version}" ]] && version_ge "${python_version}" "3.11"; then
    ok "Python ${python_version} at ${PYTHON_BIN} (minimum 3.11)"
  else
    fail_evidence "Python ${python_version:-unknown} is too old; version 3.11 or newer is required"
    MISSING_EVIDENCE+=(python3)
  fi
else
  fail_evidence 'Python 3 is not available'
  MISSING_EVIDENCE+=(python3)
fi

for file in Makefile provenance/source-commits.json configs/reproduction/paper-experiments.json scripts/reproduce/run_baselines.sh scripts/reproduce/run_bo.sh scripts/reproduce/run_dse.sh scripts/reproduce/replay_selected.sh; do
  if [[ -f "${AE_ROOT}/${file}" ]]; then
    ok "Repository input present: ${file}"
  else
    fail_evidence "Repository input is missing: ${file}"
  fi
done

printf '\n%s\n' '--- Web console prerequisites ---'

if [[ -n "${PYTHON_BIN}" ]] && "${PYTHON_BIN}" -c 'import venv, ensurepip' >/dev/null 2>&1; then
  ok 'Python venv and ensurepip modules are available'
else
  fail_web 'Python venv/ensurepip support is missing; web-demo/start.sh cannot create its environment'
  MISSING_WEB+=(python-venv)
fi

if [[ -n "$(command_path git)" ]]; then
  ok "Git available at $(command_path git)"
else
  fail_web 'Git is missing (required to obtain and bootstrap the artifact)'
  MISSING_WEB+=(git)
fi

printf '\n%s\n' '--- Fresh paper-experiment build prerequisites ---'

for command in rsync gcc g++ cmake bison flex swig; do
  tool_path="$(resolved_tool_path "${command}" || true)"
  if [[ -n "${tool_path}" ]]; then
    ok "${command}: ${tool_path}"
  else
    missing_build "${command} is missing"
    MISSING_SMOKE+=("${command}")
  fi
done

if [[ -n "$(command_path cmake)" ]]; then
  cmake_version="$(cmake --version 2>/dev/null | head -n 1 | first_version)"
  if [[ -n "${cmake_version}" ]] && version_ge "${cmake_version}" "3.16"; then
    ok "CMake ${cmake_version} (minimum 3.16)"
  else
    missing_build "CMake ${cmake_version:-unknown} is too old; version 3.16 or newer is required"
  fi
fi

if [[ -n "$(command_path gcc)" ]]; then
  gcc_version="$(gcc -dumpfullversion -dumpversion 2>/dev/null | head -n 1)"
  if [[ -n "${gcc_version}" ]] && version_ge "${gcc_version}" "9"; then
    ok "GCC ${gcc_version} (minimum 9)"
  else
    missing_build "GCC ${gcc_version:-unknown} is too old; version 9 or newer is required"
  fi
fi

memory_kib="$(awk '/MemTotal:/ {print $2}' /proc/meminfo 2>/dev/null || echo 0)"
memory_gib=$((memory_kib / 1024 / 1024))
if (( memory_gib >= 64 )); then
  ok "Memory: ${memory_gib} GiB (author reference: 314 GiB; reduce parallelism as needed)"
else
  warn "Memory: ${memory_gib} GiB. No universal minimum is claimed; OpenROAD can exceed 2 GiB and full parallel experiments need server-class headroom (author reference: 314 GiB)."
fi

disk_kib="$(df -Pk "${AE_ROOT}" 2>/dev/null | awk 'NR==2 {print $4}' || echo 0)"
disk_gib=$((disk_kib / 1024 / 1024))
if (( disk_gib >= 100 )); then
  ok "Free disk near repository: ${disk_gib} GiB (100+ GiB planning threshold; author filesystem: 22 TiB)"
else
  warn "Free disk near repository: ${disk_gib} GiB. The prior 10 GiB smoke estimate is not sufficient guidance for builds, ODBs, 3,600 BO trials, and DSE state."
fi

printf '\n%s\n' '--- Prepared workspace state ---'

if [[ -d "${ORFS_ROOT}/flow" && -d "${ORFS_ROOT}/tools/OpenROAD" ]]; then
  ok "ORFS workspace found: ${ORFS_ROOT}"
  if [[ -x "${ORFS_ROOT}/etc/DependencyInstaller.sh" ]]; then
    ok 'Pinned ORFS dependency installer is available'
  else
    warn "Pinned dependency installer is missing: ${ORFS_ROOT}/etc/DependencyInstaller.sh"
  fi
else
  warn "ORFS workspace is not prepared: ${ORFS_ROOT}"
  SMOKE_ERRORS=$((SMOKE_ERRORS + 1))
fi

YOSYS_BIN="${YOSYS_EXE:-${STATE_ROOT}/yosys/8449dd470/bin/yosys}"
OPENROAD_BIN="${OPENROAD_EXE:-${STATE_ROOT}/openroad_core/d5ff63a/install/OpenROAD/bin/openroad}"

if [[ -x "${YOSYS_BIN}" ]]; then
  ok "Pinned Yosys binary found: ${YOSYS_BIN}"
else
  warn "Pinned Yosys binary is not built: ${YOSYS_BIN}"
  SMOKE_ERRORS=$((SMOKE_ERRORS + 1))
fi

if [[ -x "${OPENROAD_BIN}" ]]; then
  ok "Pinned OpenROAD binary found: ${OPENROAD_BIN}"
else
  warn "Pinned OpenROAD binary is not built: ${OPENROAD_BIN}"
  SMOKE_ERRORS=$((SMOKE_ERRORS + 1))
fi

printf '\n%s\n' '--- Suggested commands (nothing is executed automatically) ---'

if (( ${#MISSING_EVIDENCE[@]} > 0 || ${#MISSING_WEB[@]} > 0 )); then
  case "${OS_ID}" in
    ubuntu|debian)
      printf '%s\n' '  Install the evidence and web-console prerequisites:'
      printf '%s\n' '  sudo apt-get update && sudo apt-get install -y bash git make python3 python3-venv python3-pip rsync'
      ;;
    rocky|rhel|centos|almalinux|fedora)
      printf '%s\n' '  Install the evidence and web-console prerequisites:'
      printf '%s\n' '  sudo dnf install -y bash git make python3 python3-pip rsync'
      ;;
    *)
      printf '%s\n' "  Install Bash 4+, GNU Make 4+, Git, rsync, Python 3.11+, pip, and Python venv using the ${OS_ID} package manager."
      ;;
  esac
else
  printf '%s\n' '  Repository/web prerequisites are present. No system install command is needed.'
fi

if [[ ! -d "${ORFS_ROOT}/flow" ]]; then
  printf '%s\n' '  Prepare the pinned EDA workspace:'
  printf '%s\n' '  make bootstrap'
fi

if (( ${#MISSING_SMOKE[@]} > 0 )); then
  SMOKE_PACKAGES=()
  case "${OS_ID}" in
    ubuntu|debian)
      for missing in "${MISSING_SMOKE[@]}"; do
        case "${missing}" in
          gcc|g++) package='build-essential' ;;
          *) package="${missing}" ;;
        esac
        [[ " ${SMOKE_PACKAGES[*]} " == *" ${package} "* ]] || SMOKE_PACKAGES+=("${package}")
      done
      printf '%s\n' '  Install the missing build commands detected above:'
      printf '  sudo apt-get update && sudo apt-get install -y'
      printf ' %q' "${SMOKE_PACKAGES[@]}"
      printf '\n'
      ;;
    rocky|rhel|centos|almalinux|fedora)
      for missing in "${MISSING_SMOKE[@]}"; do
        case "${missing}" in
          g++) package='gcc-c++' ;;
          *) package="${missing}" ;;
        esac
        [[ " ${SMOKE_PACKAGES[*]} " == *" ${package} "* ]] || SMOKE_PACKAGES+=("${package}")
      done
      printf '%s\n' '  Install the missing build commands detected above:'
      printf '  sudo dnf install -y'
      printf ' %q' "${SMOKE_PACKAGES[@]}"
      printf '\n'
      ;;
    *)
      printf '%s\n' '  Install a C++17 compiler, CMake 3.16+, Ninja, Bison, Flex, SWIG, Tcl/Tk, Boost, Eigen, spdlog, zlib, libffi, and rsync.'
      ;;
  esac
fi

if [[ -x "${ORFS_ROOT}/etc/DependencyInstaller.sh" && ( ${STRICT_SMOKE} -eq 1 || ${BUILD_ERRORS} -gt 0 || ${SMOKE_ERRORS} -gt 0 ) ]]; then
  printf '%s\n' '  For the complete dependency set pinned with ORFS, review and run:'
  printf '  sudo %q -all\n' "${ORFS_ROOT}/etc/DependencyInstaller.sh"
fi

if [[ ! -x "${YOSYS_BIN}" || ! -x "${OPENROAD_BIN}" ]]; then
  printf '%s\n' '  After prerequisites and bootstrap complete:'
  printf '%s\n' '  make build-tools THREADS=16'
fi

printf '\n%s\n' '--- Readiness summary ---'
if (( EVIDENCE_ERRORS == 0 )); then
  printf '%s\n' '  Repository commands:   READY'
else
  printf '%s\n' '  Repository commands:   NOT READY'
fi
if (( WEB_ERRORS == 0 )); then
  printf '%s\n' '  Web console startup:  READY'
else
  printf '%s\n' '  Web console startup:  NOT READY'
fi
if (( BUILD_ERRORS == 0 )); then
  printf '%s\n' '  EDA rebuild tools:    READY'
else
  printf '%s\n' '  EDA rebuild tools:    NEEDS ATTENTION (see commands above)'
fi
if (( SMOKE_ERRORS == 0 )); then
  printf '%s\n' '  Paper EDA runtime:     READY'
else
  printf '%s\n' '  Paper EDA runtime:     NOT READY (run bootstrap/build-tools)'
fi
if (( STRICT_SMOKE == 1 )); then
  if (( SMOKE_ERRORS == 0 && BUILD_ERRORS == 0 )); then
    printf '%s\n' '  Strict smoke doctor:  PASS'
  else
    printf '%s\n' '  Strict smoke doctor:  FAIL'
  fi
fi
printf '  Checks: %d OK, %d warnings, %d errors\n' "${PASS_COUNT}" "${WARN_COUNT}" "${FAIL_COUNT}"
printf '%s\n' '============================================================'

if (( EVIDENCE_ERRORS > 0 || WEB_ERRORS > 0 )); then
  exit 1
fi
if (( STRICT_SMOKE == 1 && (SMOKE_ERRORS > 0 || BUILD_ERRORS > 0) )); then
  exit 1
fi
exit 0
