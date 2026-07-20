#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AGENT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${AGENT_ROOT}/scripts/runtime_env.sh"
dpl_init_runtime "run_bo_9case_openroad_dpl.sh"

PYTHON_BIN="${RAYTUNE_PYTHON:-${AGENT_ROOT}/.venv_raytune/bin/python}"
SPACE="${BO_SPACE:-${AGENT_ROOT}/configs/bo_search_spaces/openroad_dpl_native.yaml}"
FLOW_VARIANT="${BO_FLOW_VARIANT:-place_batch_20260421_220319}"
TRIALS="${BO_TRIALS:-400}"
MAX_CONCURRENT_CASES="${BO_MAX_CONCURRENT_CASES:-3}"
MAX_CONCURRENT_TRIALS="${BO_MAX_CONCURRENT_TRIALS:-4}"
THREADS="${BO_THREADS_PER_TRIAL:-10}"
RAY_CPUS="${BO_RAY_CPUS:-${MAX_CONCURRENT_TRIALS}}"
SEED="${BO_SEED:-1}"
STARTUP_TRIALS="${BO_STARTUP_TRIALS:-}"
TPE_CANDIDATES="${BO_TPE_CANDIDATES:-64}"
ANCHOR_STRATEGY="${BO_ANCHOR_STRATEGY:-mechanism}"
RUN_PREFIX="${BO_RUN_PREFIX:-openroad_dpl_native_9case_bo}"
LEGALIZE_TIMEOUT="${BO_LEGALIZE_TIMEOUT_SECONDS:-}"
RUN_STAMP="$(date +%Y%m%d_%H%M%S)"
SWEEP_ROOT="${DPL_EVOLVE_STATE_ROOT:-${AGENT_ROOT}/.dpl_evolve_state}/bo_sweeps/${RUN_PREFIX}_${FLOW_VARIANT}_${RUN_STAMP}"
STATUS_FILE="${SWEEP_ROOT}/status.tsv"
LOG_DIR="${SWEEP_ROOT}/logs"
CASE_SET="${BO_CASE_SET:-bo_9case}"
CASES=()

usage() {
  cat <<'EOF'
Usage: run_bo_9case_openroad_dpl.sh

Environment overrides:
  BO_CASE_SET                  Default: bo_9case
  BO_FLOW_VARIANT              Default: place_batch_20260421_220319
  BO_TRIALS                    Default: 400
  BO_MAX_CONCURRENT_CASES      Default: 3
  BO_MAX_CONCURRENT_TRIALS     Default: 4
  BO_THREADS_PER_TRIAL         Default: 10
  BO_RAY_CPUS                  Default: BO_MAX_CONCURRENT_TRIALS
  BO_SPACE                     Default: configs/bo_search_spaces/openroad_dpl_native.yaml
  BO_RUN_PREFIX                Default: openroad_dpl_native_9case_bo
  BO_SEED                      Default: 1

This launches 9 independent OpenROAD-DPL BO jobs.  At most three cases run at
once; each case uses Ray Tune to run at most four candidate placements in
parallel.  The script expects every case to already have the requested
FLOW_VARIANT/3_4_place_resized.odb prepared.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --case-set) CASE_SET="$2"; shift 2 ;;
    --case) CASES+=("$2"); shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) echo "[ERROR] Unknown argument: $1" >&2; usage >&2; exit 1 ;;
  esac
done

if [[ "${#CASES[@]}" -eq 0 ]]; then
  mapfile -t CASES < <("${DPL_EVOLVE_PYTHON}" "${AGENT_ROOT}/scripts/repo/case_registry.py" --case-set "${CASE_SET}" --field case)
fi

if [[ ! -x "${PYTHON_BIN}" ]]; then
  echo "[ERROR] Ray Tune Python is not executable: ${PYTHON_BIN}" >&2
  echo "        Run ${AGENT_ROOT}/scripts/bo/setup_raytune_venv.sh first." >&2
  exit 1
fi

for value_name in TRIALS MAX_CONCURRENT_CASES MAX_CONCURRENT_TRIALS THREADS RAY_CPUS SEED; do
  value="${!value_name}"
  if ! [[ "${value}" =~ ^[0-9]+$ ]] || [[ "${value}" -lt 1 ]]; then
    echo "[ERROR] ${value_name} must be a positive integer, got '${value}'" >&2
    exit 2
  fi
done

mkdir -p "${LOG_DIR}"
printf "case\tstatus\tlog\tstart\tend\trun_id\n" > "${STATUS_FILE}"

check_case_snapshot() {
  local case_id="$1"
  local design platform snapshot
  design="$("${DPL_EVOLVE_PYTHON}" "${AGENT_ROOT}/scripts/repo/case_registry.py" --case "${case_id}" --field design)"
  platform="$("${DPL_EVOLVE_PYTHON}" "${AGENT_ROOT}/scripts/repo/case_registry.py" --case "${case_id}" --field platform)"
  snapshot="${ORFS_ROOT}/flow/results/${platform}/${design}/${FLOW_VARIANT}/3_4_place_resized.odb"
  if [[ ! -f "${snapshot}" ]]; then
    echo "[ERROR] Missing input snapshot for ${case_id}: ${snapshot}" >&2
    return 1
  fi
}

run_case() {
  local case_id="$1"
  local run_id log start_ts end_ts rc
  local -a cmd
  run_id="${RUN_PREFIX}_${FLOW_VARIANT}_${case_id}"
  log="${LOG_DIR}/${case_id}.log"
  start_ts="$(date '+%F %T')"

  cmd=(
    "${PYTHON_BIN}" "${AGENT_ROOT}/scripts/bo/bo_tune_case.py"
    --case "${case_id}"
    --flow-variant "${FLOW_VARIANT}"
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

  set +e
  {
    printf "[START] %s\n" "${start_ts}"
    printf "[INFO] case=%s\n" "${case_id}"
    printf "[INFO] flow_variant=%s\n" "${FLOW_VARIANT}"
    printf "[INFO] run_id=%s\n" "${run_id}"
    printf "[INFO] trials=%s max_concurrent_trials=%s threads=%s ray_cpus=%s\n" \
      "${TRIALS}" "${MAX_CONCURRENT_TRIALS}" "${THREADS}" "${RAY_CPUS}"
    printf "[INFO] command:"
    printf " %q" "${cmd[@]}"
    printf "\n"
    "${cmd[@]}"
  } > "${log}" 2>&1
  rc=$?
  set -e
  end_ts="$(date '+%F %T')"

  if [[ ${rc} -eq 0 ]]; then
    printf "%s\tPASS\t%s\t%s\t%s\t%s\n" "${case_id}" "${log}" "${start_ts}" "${end_ts}" "${run_id}" >> "${STATUS_FILE}"
  else
    printf "%s\tFAIL(%s)\t%s\t%s\t%s\t%s\n" "${case_id}" "${rc}" "${log}" "${start_ts}" "${end_ts}" "${run_id}" >> "${STATUS_FILE}"
  fi
  return "${rc}"
}

echo "BO 9-case OpenROAD-DPL sweep"
echo "  flow_variant         : ${FLOW_VARIANT}"
echo "  trials/case          : ${TRIALS}"
echo "  concurrent cases     : ${MAX_CONCURRENT_CASES}"
echo "  concurrent trials    : ${MAX_CONCURRENT_TRIALS}"
echo "  threads/trial        : ${THREADS}"
echo "  ray cpus/case        : ${RAY_CPUS}"
echo "  case_set             : ${CASE_SET}"
echo "  cases                : ${CASES[*]}"
echo "  sweep_root           : ${SWEEP_ROOT}"
echo

for case_id in "${CASES[@]}"; do
  check_case_snapshot "${case_id}"
done

declare -a pids=()
overall_rc=0

for case_id in "${CASES[@]}"; do
  while (( $(jobs -pr | wc -l) >= MAX_CONCURRENT_CASES )); do
    if ! wait -n; then
      overall_rc=1
    fi
  done
  echo "[INFO] launch case=${case_id}"
  run_case "${case_id}" &
  pids+=("$!")
done

for pid in "${pids[@]}"; do
  if ! wait "${pid}"; then
    overall_rc=1
  fi
done

echo
echo "BO 9-case sweep finished. Status:"
cat "${STATUS_FILE}"
exit "${overall_rc}"
