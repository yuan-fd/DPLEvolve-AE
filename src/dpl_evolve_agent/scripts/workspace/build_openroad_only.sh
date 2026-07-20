#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOOTSTRAP_AGENT_ROOT="${DPL_EVOLVE_AGENT_ROOT:-$(cd "${SCRIPT_DIR}/../.." && pwd)}"
BOOTSTRAP_AGENT_ROOT="$(realpath -m "${BOOTSTRAP_AGENT_ROOT}")"
source "${BOOTSTRAP_AGENT_ROOT}/scripts/runtime_env.sh"
dpl_init_runtime "build_openroad_only.sh"
echo "[INFO] runtime_orfs_build_root=${ORFS_BUILD_ROOT:-}"
ROOT_DIR="${ORFS_ROOT}"
THREADS="8"
INCREMENTAL_TARGETS_RAW="openroad sta"
INITIAL_CMAKE_ARGS_RAW=""
DISABLE_TESTS_RAW="1"
DISABLE_GUI_RAW="0"
INSTALL_MODE="binaries"
CORE_ROOT_RAW=""
BUILD_DIR_RAW=""
INSTALL_ROOT_RAW=""
INSTALL_BIN_RAW=""
GENERATOR=""
INSTALL_RPATH=""
BUILD_ROOT="${ORFS_BUILD_ROOT:-${ORFS_ROOT}}"
if [[ "${ORFS_ROOT}" == *" "* && "${BUILD_ROOT}" == "${ORFS_ROOT}" ]]; then
  BUILD_ROOT="$(_dpl_stable_alias_root)"
  export ORFS_BUILD_ROOT="${BUILD_ROOT}"
fi

usage() {
  cat <<'EOF'
Usage: build_openroad_only.sh [options] [threads]

Internal helper for building the reusable common-core OpenROAD binary. The
normal per-agent compile path is build_openroad_variant_relink.py.

Options:
  --threads N              Build threads. Default: 8.
  --build-dir PATH         Build directory. Default: <orfs>/tools/OpenROAD/build.
  --install-root PATH      Install root. Default: <orfs>/tools/install/OpenROAD.
  --openroad-binary PATH   Output OpenROAD binary. Default: <install-root>/bin/openroad.
  --core-root PATH         Optional common-core seed to copy from.
  --targets "A B"          CMake targets. Default: "openroad sta".
  --cmake-arg ARG          Extra CMake configure argument. May be repeated.
  --generator NAME         CMake generator.
  --install-rpath PATHS    CMAKE_INSTALL_RPATH value.
  --install-mode MODE      binaries or cmake. Default: binaries.
  --enable-tests           Configure with ENABLE_TESTS=ON.
  --disable-tests          Configure with ENABLE_TESTS=OFF. Default.
  --enable-gui             Configure with BUILD_GUI=ON. Default.
  --disable-gui            Configure with BUILD_GUI=OFF.
  --help                   Show this message.

OPENROAD_* environment variables are intentionally not used as path defaults.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --threads)
      THREADS="$2"
      shift 2
      ;;
    --build-dir)
      BUILD_DIR_RAW="$2"
      shift 2
      ;;
    --install-root)
      INSTALL_ROOT_RAW="$2"
      shift 2
      ;;
    --openroad-binary)
      INSTALL_BIN_RAW="$2"
      shift 2
      ;;
    --core-root)
      CORE_ROOT_RAW="$2"
      shift 2
      ;;
    --targets)
      INCREMENTAL_TARGETS_RAW="$2"
      shift 2
      ;;
    --cmake-arg)
      INITIAL_CMAKE_ARGS_RAW+="${INITIAL_CMAKE_ARGS_RAW:+ }$2"
      shift 2
      ;;
    --generator)
      GENERATOR="$2"
      shift 2
      ;;
    --install-rpath)
      INSTALL_RPATH="$2"
      shift 2
      ;;
    --install-mode)
      INSTALL_MODE="$2"
      shift 2
      ;;
    --enable-tests)
      DISABLE_TESTS_RAW="0"
      shift
      ;;
    --disable-tests)
      DISABLE_TESTS_RAW="1"
      shift
      ;;
    --enable-gui)
      DISABLE_GUI_RAW="0"
      shift
      ;;
    --disable-gui)
      DISABLE_GUI_RAW="1"
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      if [[ "$1" =~ ^[0-9]+$ ]]; then
        THREADS="$1"
        shift
      else
        echo "[ERROR] Unknown argument: $1" >&2
        usage >&2
        exit 1
      fi
      ;;
  esac
done

BUILD_DIR="${BUILD_DIR_RAW:-${BUILD_ROOT}/tools/OpenROAD/build}"
INSTALL_ROOT="${INSTALL_ROOT_RAW:-${BUILD_ROOT}/tools/install/OpenROAD}"
INSTALL_BIN="${INSTALL_BIN_RAW:-${INSTALL_ROOT}/bin/openroad}"
OPENROAD_SRC="${BUILD_ROOT}/tools/OpenROAD"
CORE_ROOT=""
if [[ -n "${CORE_ROOT_RAW}" ]]; then
  CORE_ROOT="$(realpath -m "${CORE_ROOT_RAW}")"
fi

read -r -a INCREMENTAL_TARGETS <<< "${INCREMENTAL_TARGETS_RAW}"
read -r -a INITIAL_CMAKE_ARGS <<< "${INITIAL_CMAKE_ARGS_RAW}"

is_truthy() {
  local value="${1,,}"
  [[ "${value}" == "1" || "${value}" == "true" || "${value}" == "yes" || "${value}" == "on" ]]
}

if [[ ! -f "${OPENROAD_SRC}/CMakeLists.txt" ]]; then
  echo "[ERROR] Missing OpenROAD source tree under ${OPENROAD_SRC}" >&2
  exit 1
fi

if [[ -d "${BUILD_ROOT}/dependencies" && -f "${BUILD_ROOT}/dev_env.sh" ]]; then
  # shellcheck disable=SC1090
  . "${BUILD_ROOT}/dev_env.sh"
fi

export CC="${CC:-$(command -v gcc)}"
export CXX="${CXX:-$(command -v g++)}"

BUILD_NEEDS_CONFIGURE=0

seed_from_core_if_needed() {
  if [[ -z "${CORE_ROOT_RAW}" ]]; then
    return 0
  fi
  local core_build_dir core_install_root
  core_build_dir="${CORE_ROOT}/build"
  core_install_root="${CORE_ROOT}/install/OpenROAD"
  if [[ "$(realpath -m "${BUILD_DIR}")" == "$(realpath -m "${core_build_dir}")" \
     && "$(realpath -m "${INSTALL_ROOT}")" == "$(realpath -m "${core_install_root}")" ]]; then
    return 0
  fi
  if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    return 0
  fi
  if [[ ! -f "${core_build_dir}/CMakeCache.txt" ]]; then
    echo "[ERROR] Explicit core root is missing seeded build cache: ${core_build_dir}" >&2
    exit 1
  fi
  if [[ ! -x "${core_install_root}/bin/openroad" ]]; then
    echo "[ERROR] Explicit core root is missing core binary: ${core_install_root}/bin/openroad" >&2
    exit 1
  fi
  echo "[INFO] Seeding variant build/install from common core."
  echo "[INFO] core_root=${CORE_ROOT}"
  echo "[INFO] variant_build_dir=${BUILD_DIR}"
  echo "[INFO] variant_install_root=${INSTALL_ROOT}"
  mkdir -p "${BUILD_DIR}" "${INSTALL_ROOT}"
  rsync -a --delete "${core_build_dir}/" "${BUILD_DIR}/"
  rsync -a --delete "${core_install_root}/" "${INSTALL_ROOT}/"
  rm -f "${BUILD_DIR}/CMakeCache.txt" "${BUILD_DIR}/Makefile" "${BUILD_DIR}/cmake_install.cmake"
  if [[ -d "${BUILD_DIR}/CMakeFiles" ]]; then
    find "${BUILD_DIR}/CMakeFiles" -maxdepth 1 -type f -delete
  fi
}

inspect_build_tree() {
  local cache_file="$1/CMakeCache.txt"
  local expected_build_dir expected_home_dir cache_build_dir cache_home_dir
  local expected_build_dir_resolved expected_home_dir_resolved
  local cache_build_dir_resolved cache_home_dir_resolved
  expected_build_dir="$(realpath -m "$1")"
  expected_home_dir="$(realpath -m "${BUILD_ROOT}/tools/OpenROAD")"
  expected_build_dir_resolved="$(realpath -m "${expected_build_dir}")"
  expected_home_dir_resolved="$(realpath -m "${expected_home_dir}")"

  cache_build_dir="$(awk -F= '/^CMAKE_CACHEFILE_DIR:INTERNAL=/{print $2}' "${cache_file}" | tail -n 1)"
  cache_home_dir="$(awk -F= '/^CMAKE_HOME_DIRECTORY:INTERNAL=/{print $2}' "${cache_file}" | tail -n 1)"
  cache_build_dir_resolved="$(realpath -m "${cache_build_dir}")"
  cache_home_dir_resolved="$(realpath -m "${cache_home_dir}")"

  if [[ -n "${cache_home_dir}" && "${cache_home_dir_resolved}" != "${expected_home_dir_resolved}" ]]; then
    echo "[INFO] Existing OpenROAD build tree belongs to another workspace."
    echo "[INFO] cached_home_dir=${cache_home_dir}"
    echo "[INFO] expected_home_dir=${expected_home_dir}"
    echo "[INFO] Resetting build tree at ${expected_build_dir_resolved}"
    rm -rf "${expected_build_dir_resolved}"
    return 0
  fi

  if [[ -n "${cache_build_dir}" && "${cache_build_dir_resolved}" != "${expected_build_dir_resolved}" ]]; then
    echo "[INFO] OpenROAD build cache needs reconfigure for this build dir."
    echo "[INFO] cached_build_dir=${cache_build_dir}"
    echo "[INFO] expected_build_dir=${expected_build_dir}"
    BUILD_NEEDS_CONFIGURE=1
  fi
}

seed_from_core_if_needed

if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
  inspect_build_tree "${BUILD_DIR}"
fi

if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
  BUILD_NEEDS_CONFIGURE=1
fi

if [[ "${BUILD_NEEDS_CONFIGURE}" == "1" ]]; then
  echo "[INFO] Configuring OpenROAD build tree."
  echo "[INFO] build_dir=${BUILD_DIR}"
  echo "[INFO] install_root=${INSTALL_ROOT}"
  mkdir -p "${BUILD_DIR}" "${INSTALL_ROOT}"
  configure_cmd=(
    cmake
    -S "${OPENROAD_SRC}"
    -B "${BUILD_DIR}"
    -D "CMAKE_INSTALL_PREFIX=${INSTALL_ROOT}"
    -D "CMAKE_EXPORT_COMPILE_COMMANDS=ON"
  )
  if [[ -n "${GENERATOR}" ]]; then
    configure_cmd+=(-G "${GENERATOR}")
  fi
  if is_truthy "${DISABLE_TESTS_RAW}"; then
    configure_cmd+=(-D "ENABLE_TESTS=OFF")
  fi
  if is_truthy "${DISABLE_GUI_RAW}"; then
    configure_cmd+=(-D "BUILD_GUI=OFF")
  fi
  if [[ -n "${INSTALL_RPATH}" ]]; then
    configure_cmd+=(
      -D "CMAKE_INSTALL_RPATH=${INSTALL_RPATH}"
      -D "CMAKE_INSTALL_RPATH_USE_LINK_PATH=TRUE"
    )
  fi
  if [[ ${#INITIAL_CMAKE_ARGS[@]} -gt 0 ]]; then
    configure_cmd+=("${INITIAL_CMAKE_ARGS[@]}")
  fi
  echo "[INFO] disable_tests=${DISABLE_TESTS_RAW} disable_gui=${DISABLE_GUI_RAW}"
  printf '[INFO] configure_cmd:'
  printf ' %q' "${configure_cmd[@]}"
  printf '\n'
  "${configure_cmd[@]}"
fi

echo "[INFO] Reusing existing OpenROAD build tree with ${THREADS} threads."
echo "[INFO] orfs_root=${ROOT_DIR}"
echo "[INFO] build_root=${BUILD_ROOT}"
echo "[INFO] build_dir=${BUILD_DIR}"
echo "[INFO] install_bin=${INSTALL_BIN}"
echo "[INFO] incremental_targets=${INCREMENTAL_TARGETS[*]}"
if [[ -n "${CORE_ROOT_RAW}" ]]; then
  echo "[INFO] core_root=${CORE_ROOT}"
fi
cmake --build "${BUILD_DIR}" --target "${INCREMENTAL_TARGETS[@]}" -j "${THREADS}"

case "${INSTALL_MODE}" in
  binaries)
    mkdir -p "$(dirname "${INSTALL_BIN}")" "${INSTALL_ROOT}/bin"
    if [[ ! -x "${BUILD_DIR}/bin/openroad" ]]; then
      echo "[ERROR] Built OpenROAD binary missing: ${BUILD_DIR}/bin/openroad" >&2
      exit 1
    fi
    # Do not copy directly over a running OpenROAD executable: Linux can return
    # ETXTBSY when another experiment is using the shared seed binary.  Install
    # via same-directory rename so active processes keep their old inode and new
    # launches see the refreshed binary.
    tmp_openroad="${INSTALL_BIN}.tmp.$$"
    cp -p "${BUILD_DIR}/bin/openroad" "${tmp_openroad}"
    mv -f "${tmp_openroad}" "${INSTALL_BIN}"
    if [[ -x "${BUILD_DIR}/src/sta/sta" ]]; then
      tmp_sta="${INSTALL_ROOT}/bin/sta.tmp.$$"
      cp -p "${BUILD_DIR}/src/sta/sta" "${tmp_sta}"
      mv -f "${tmp_sta}" "${INSTALL_ROOT}/bin/sta"
    fi
    ;;
  cmake)
    cmake --install "${BUILD_DIR}" --prefix "${INSTALL_ROOT}"
    ;;
  *)
    echo "[ERROR] Unknown install mode: ${INSTALL_MODE}; expected binaries or cmake." >&2
    exit 1
    ;;
esac

if [[ ! -x "${INSTALL_BIN}" ]]; then
  echo "[ERROR] OpenROAD binary missing after install target: ${INSTALL_BIN}" >&2
  exit 1
fi

echo "[INFO] OpenROAD-only build complete: ${INSTALL_BIN}"
