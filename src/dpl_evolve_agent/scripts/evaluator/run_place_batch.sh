#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOOTSTRAP_AGENT_ROOT="${DPL_EVOLVE_AGENT_ROOT:-$(cd "${SCRIPT_DIR}/../.." && pwd)}"
BOOTSTRAP_AGENT_ROOT="$(realpath -m "${BOOTSTRAP_AGENT_ROOT}")"
source "${BOOTSTRAP_AGENT_ROOT}/scripts/runtime_env.sh"

MAX_TASKS="5"
NUM_CORES="16"
TARGET="place"
CASE_SET="place_batch"
FLOW_VARIANT="place_batch_$(date +%Y%m%d_%H%M%S)"
RUN_STAMP="$(date +%Y%m%d_%H%M%S)"
CASE_ID=""

usage() {
  cat <<'EOF'
Usage: run_place_batch.sh [options]

Options:
  --case-set NAME       Case set from problems/case_sets.json. Default: place_batch.
  --case ID             Single case override. The case must have problems/<id>/problem.yaml.
  --max-tasks N         Max concurrent jobs. Default: 5.
  --num-cores N         Threads per ORFS invocation. Default: 16.
  --target NAME         ORFS target. Default: place.
  --flow-variant NAME   Shared FLOW_VARIANT. Default: place_batch_<timestamp>.
  --help                Show this message.

This script prepares initial place snapshots. Case facts come only from
problems/<case>/problem.yaml; this script does not maintain design-config
lists.
EOF
}

dpl_init_runtime "run_place_batch.sh"
REPO_ROOT="${ORFS_ROOT}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --case-set)
      CASE_SET="$2"
      shift 2
      ;;
    --case)
      CASE_ID="$2"
      shift 2
      ;;
    --max-tasks)
      MAX_TASKS="$2"
      shift 2
      ;;
    --num-cores)
      NUM_CORES="$2"
      shift 2
      ;;
    --target)
      TARGET="$2"
      shift 2
      ;;
    --flow-variant)
      FLOW_VARIANT="$2"
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "[ERROR] Unknown argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

FLOW_DIR="${REPO_ROOT}/flow"
RUN_DIR="${FLOW_DIR}/logs/batch_runs/${FLOW_VARIANT}_${RUN_STAMP}"
STATUS_FILE="${RUN_DIR}/status.tsv"
SUMMARY_FILE="${RUN_DIR}/summary.txt"
cd "${FLOW_DIR}"

mkdir -p "${RUN_DIR}"
printf "config\tstatus\tlog\tstart\tend\n" > "${STATUS_FILE}"

if [[ -n "${CASE_ID}" ]]; then
  mapfile -t DESIGN_CONFIGS < <(
    "${DPL_EVOLVE_PYTHON}" "${DPL_EVOLVE_AGENT_ROOT}/scripts/repo/case_registry.py" --case "${CASE_ID}" --field design_config
  )
else
  mapfile -t DESIGN_CONFIGS < <(
    "${DPL_EVOLVE_PYTHON}" "${DPL_EVOLVE_AGENT_ROOT}/scripts/repo/case_registry.py" --case-set "${CASE_SET}" --field design_config
  )
fi

label_from_config() {
  local config="$1"
  local label
  label="${config#./designs/}"
  label="${label%/config.mk}"
  label="${label//\//__}"
  printf "%s\n" "${label}"
}

run_case() {
  local config="$1"
  local label log start_ts end_ts rc

  label="$(label_from_config "${config}")"
  log="${RUN_DIR}/${label}.log"
  start_ts="$(date '+%F %T')"

  set +e
  {
    printf "[START] %s\n" "${start_ts}"
    printf "[INFO] config=%s\n" "${config}"
    printf "[INFO] target=%s max_tasks=%s num_cores=%s flow_variant=%s\n" \
      "${TARGET}" "${MAX_TASKS}" "${NUM_CORES}" "${FLOW_VARIANT}"
    printf "[INFO] workdir=%s\n" "${FLOW_DIR}"
    make \
      DESIGN_CONFIG="${config}" \
      FLOW_VARIANT="${FLOW_VARIANT}" \
      NUM_CORES="${NUM_CORES}" \
      check-openroad check-yosys "${TARGET}"
  } > "${log}" 2>&1
  rc=$?
  set -e
  end_ts="$(date '+%F %T')"

  if [[ ${rc} -eq 0 ]]; then
    printf "%s\tPASS\t%s\t%s\t%s\n" "${config}" "${log}" "${start_ts}" "${end_ts}" >> "${STATUS_FILE}"
  else
    printf "%s\tFAIL(%s)\t%s\t%s\t%s\n" "${config}" "${rc}" "${log}" "${start_ts}" "${end_ts}" >> "${STATUS_FILE}"
  fi

  return "${rc}"
}

write_summary() {
  local passed failed total
  total=$(tail -n +2 "${STATUS_FILE}" | wc -l | tr -d ' ')
  passed=$(awk -F '\t' 'NR > 1 && $2 == "PASS" { count++ } END { print count + 0 }' "${STATUS_FILE}")
  failed=$(awk -F '\t' 'NR > 1 && $2 != "PASS" { count++ } END { print count + 0 }' "${STATUS_FILE}")

  {
    printf "FLOW_VARIANT=%s\n" "${FLOW_VARIANT}"
    printf "TARGET=%s\n" "${TARGET}"
    printf "MAX_TASKS=%s\n" "${MAX_TASKS}"
    printf "NUM_CORES=%s\n" "${NUM_CORES}"
    printf "RUN_DIR=%s\n" "${RUN_DIR}"
    printf "TOTAL=%s\n" "${total}"
    printf "PASSED=%s\n" "${passed}"
    printf "FAILED=%s\n" "${failed}"
  } > "${SUMMARY_FILE}"
}

if ! [[ "${MAX_TASKS}" =~ ^[1-9][0-9]*$ ]]; then
  echo "MAX_TASKS must be a positive integer, got '${MAX_TASKS}'" >&2
  exit 2
fi

if ! [[ "${NUM_CORES}" =~ ^[1-9][0-9]*$ ]]; then
  echo "NUM_CORES must be a positive integer, got '${NUM_CORES}'" >&2
  exit 2
fi

echo "Batch placement run"
echo "  target      : ${TARGET}"
if [[ -n "${CASE_ID}" ]]; then
  echo "  case        : ${CASE_ID}"
else
  echo "  case_set    : ${CASE_SET}"
fi
echo "  flow_variant: ${FLOW_VARIANT}"
echo "  max_tasks   : ${MAX_TASKS}"
echo "  num_cores   : ${NUM_CORES}"
echo "  run_dir     : ${RUN_DIR}"
echo "  cases       : ${#DESIGN_CONFIGS[@]}"
echo

echo "Checking OpenROAD and Yosys availability..."
make check-openroad check-yosys
echo

declare -a pids=()
overall_rc=0

for config in "${DESIGN_CONFIGS[@]}"; do
  while (( $(jobs -pr | wc -l) >= MAX_TASKS )); do
    if ! wait -n; then
      overall_rc=1
    fi
  done

  label="$(label_from_config "${config}")"
  echo "Launching ${label}"
  run_case "${config}" &
  pids+=("$!")
done

for pid in "${pids[@]}"; do
  if ! wait "${pid}"; then
    overall_rc=1
  fi
done

write_summary

echo
echo "Completed batch run. Summary:"
cat "${SUMMARY_FILE}"
echo
echo "Status file: ${STATUS_FILE}"

exit "${overall_rc}"
