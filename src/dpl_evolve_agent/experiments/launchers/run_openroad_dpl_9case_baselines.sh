#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AGENT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${AGENT_ROOT}/scripts/runtime_env.sh"
dpl_init_runtime "run_openroad_dpl_9case_baselines.sh"

CASE_SET="${BASELINE_CASE_SET:-bo_9case}"
FLOW_VARIANT="${BASELINE_FLOW_VARIANT:-place_batch_20260421_220319}"
LINE="${BASELINE_LINE:-openroad_dpl_flow}"
RUN_PREFIX="${BASELINE_RUN_PREFIX:-bo9_openroad_dpl_flow}"
THREADS="${BASELINE_THREADS:-10}"
MAX_PARALLEL="${BASELINE_MAX_PARALLEL:-3}"
RUN_STAMP="$(date +%Y%m%d_%H%M%S)"
SWEEP_ROOT="${DPL_EVOLVE_STATE_ROOT}/baseline_sweeps/${RUN_PREFIX}_${FLOW_VARIANT}_${RUN_STAMP}"
LOG_DIR="${SWEEP_ROOT}/logs"
STATUS_FILE="${SWEEP_ROOT}/status.tsv"
SUMMARY_FILE="${SWEEP_ROOT}/summary.tsv"
CASES=()

usage() {
  cat <<'EOF'
Usage: run_openroad_dpl_9case_baselines.sh [options]

Run OpenROAD default DPL baselines for the shared 9-case place_batch set.

Options:
  --case-set NAME      Case set from problems/case_sets.json. Default: bo_9case.
  --case ID            Run one case. May be repeated. Overrides --case-set.
  --flow-variant NAME  Default: place_batch_20260421_220319.
  --line NAME          Canonical baseline line. Default: openroad_dpl_flow.
  --run-prefix NAME    Default: bo9_openroad_dpl_flow.
  --threads N          OpenROAD threads per run. Default: 10.
  --max-parallel N     Concurrent cases. Default: 3.
  --help               Show this message.

Environment aliases use BASELINE_* names matching the option names.
The script refuses to start if any requested case is missing
FLOW_VARIANT/3_4_place_resized.odb.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --case-set) CASE_SET="$2"; shift 2 ;;
    --case) CASES+=("$2"); shift 2 ;;
    --flow-variant) FLOW_VARIANT="$2"; shift 2 ;;
    --line) LINE="$2"; shift 2 ;;
    --run-prefix) RUN_PREFIX="$2"; shift 2 ;;
    --threads) THREADS="$2"; shift 2 ;;
    --max-parallel) MAX_PARALLEL="$2"; shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) echo "[ERROR] Unknown argument: $1" >&2; usage >&2; exit 1 ;;
  esac
done

case "${LINE}" in
  openroad_dpl_flow|openroad_dpl_negotiation|evolve_default) ;;
  *) echo "[ERROR] Unsupported --line: ${LINE}" >&2; exit 1 ;;
esac

for value_name in THREADS MAX_PARALLEL; do
  value="${!value_name}"
  if ! [[ "${value}" =~ ^[0-9]+$ ]] || [[ "${value}" -lt 1 ]]; then
    echo "[ERROR] ${value_name} must be a positive integer, got '${value}'" >&2
    exit 2
  fi
done

if [[ "${#CASES[@]}" -eq 0 ]]; then
  mapfile -t CASES < <("${DPL_EVOLVE_PYTHON}" "${AGENT_ROOT}/scripts/repo/case_registry.py" --case-set "${CASE_SET}" --field case)
fi

snapshot_path() {
  local case_id="$1"
  local design platform
  design="$("${DPL_EVOLVE_PYTHON}" "${AGENT_ROOT}/scripts/repo/case_registry.py" --case "${case_id}" --field design)"
  platform="$("${DPL_EVOLVE_PYTHON}" "${AGENT_ROOT}/scripts/repo/case_registry.py" --case "${case_id}" --field platform)"
  printf '%s\n' "${ORFS_ROOT}/flow/results/${platform}/${design}/${FLOW_VARIANT}/3_4_place_resized.odb"
}

metrics_path_for_case() {
  local case_id="$1"
  local design platform
  design="$("${DPL_EVOLVE_PYTHON}" "${AGENT_ROOT}/scripts/repo/case_registry.py" --case "${case_id}" --field design)"
  platform="$("${DPL_EVOLVE_PYTHON}" "${AGENT_ROOT}/scripts/repo/case_registry.py" --case "${case_id}" --field platform)"
  printf '%s\n' "${ORFS_ROOT}/flow/reports/${platform}/${design}/${FLOW_VARIANT}/dpl_evolve_baseline/${RUN_PREFIX}_${case_id}/metrics.json"
}

for case_id in "${CASES[@]}"; do
  snapshot="$(snapshot_path "${case_id}")"
  if [[ ! -f "${snapshot}" ]]; then
    echo "[ERROR] Missing input snapshot for ${case_id}: ${snapshot}" >&2
    exit 3
  fi
done

mkdir -p "${LOG_DIR}"
printf "case\tstatus\tstart\tend\tlog\tmetrics_json\n" > "${STATUS_FILE}"

run_case() {
  local case_id="$1"
  local run_tag log start_ts end_ts rc metrics_path
  run_tag="${RUN_PREFIX}_${case_id}"
  log="${LOG_DIR}/${case_id}.log"
  start_ts="$(date '+%F %T')"

  set +e
  "${AGENT_ROOT}/baseline/run_baseline.sh" \
    --line "${LINE}" \
    --case "${case_id}" \
    --flow-variant "${FLOW_VARIANT}" \
    --run-tag "${run_tag}" \
    --threads "${THREADS}" \
    > "${log}" 2>&1
  rc=$?
  set -e
  end_ts="$(date '+%F %T')"
  metrics_path="$(sed -n 's/^[[:space:]]*metrics:[[:space:]]*//p' "${log}" | tail -1)"
  if [[ -z "${metrics_path}" ]]; then
    metrics_path="$(metrics_path_for_case "${case_id}")"
  fi

  if [[ "${rc}" -eq 0 ]]; then
    printf "%s\tPASS\t%s\t%s\t%s\t%s\n" "${case_id}" "${start_ts}" "${end_ts}" "${log}" "${metrics_path}" >> "${STATUS_FILE}"
  else
    printf "%s\tFAIL(%s)\t%s\t%s\t%s\t%s\n" "${case_id}" "${rc}" "${start_ts}" "${end_ts}" "${log}" "${metrics_path}" >> "${STATUS_FILE}"
  fi
  return "${rc}"
}

echo "OpenROAD DPL baseline sweep"
echo "  case_set       : ${CASE_SET}"
echo "  cases          : ${CASES[*]}"
echo "  flow_variant   : ${FLOW_VARIANT}"
echo "  line           : ${LINE}"
echo "  max_parallel   : ${MAX_PARALLEL}"
echo "  sweep_root     : ${SWEEP_ROOT}"
echo

declare -a pids=()
overall_rc=0
for case_id in "${CASES[@]}"; do
  while (( $(jobs -pr | wc -l) >= MAX_PARALLEL )); do
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

"${DPL_EVOLVE_PYTHON}" - "${STATUS_FILE}" "${SUMMARY_FILE}" <<'PY'
import csv
import json
import sys
from pathlib import Path

status_path = Path(sys.argv[1])
summary_path = Path(sys.argv[2])
rows = list(csv.DictReader(status_path.open(encoding="utf-8"), delimiter="\t"))
fields = [
    "case",
    "status",
    "metrics_json",
    "hpwl_global",
    "hpwl_final",
    "runtime_s",
    "avg_disp",
    "max_disp",
    "violations",
]
with summary_path.open("w", encoding="utf-8", newline="") as stream:
    writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t", lineterminator="\n")
    writer.writeheader()
    for row in rows:
        metrics_path = Path(row.get("metrics_json") or "")
        out = {
            "case": row.get("case", ""),
            "status": row.get("status", ""),
            "metrics_json": str(metrics_path),
        }
        if metrics_path.is_file():
            metrics = json.loads(metrics_path.read_text(encoding="utf-8"))
            hpwl = metrics.get("hpwl") or metrics.get("hpwl_openroad_log") or metrics.get("hpwl_proxy") or {}
            stages = metrics.get("hpwl_stages") or {}
            legality = metrics.get("legality") or {}
            out.update(
                {
                    "hpwl_global": stages.get("global_micron") or hpwl.get("before_micron") or "",
                    "hpwl_final": hpwl.get("after_micron") or "",
                    "runtime_s": metrics.get("runtime_seconds") or "",
                    "avg_disp": (metrics.get("displacement") or {}).get("avg_micron") or "",
                    "max_disp": (metrics.get("displacement") or {}).get("max_micron") or "",
                    "violations": legality.get("placement_violations", ""),
                }
            )
        writer.writerow(out)
PY

echo
echo "Baseline sweep finished. Status:"
cat "${STATUS_FILE}"
echo
echo "Summary:"
cat "${SUMMARY_FILE}"
exit "${overall_rc}"
