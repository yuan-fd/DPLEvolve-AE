#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOOTSTRAP_AGENT_ROOT="${DPL_EVOLVE_AGENT_ROOT:-$(cd "${SCRIPT_DIR}/../.." && pwd)}"
BOOTSTRAP_AGENT_ROOT="$(realpath -m "${BOOTSTRAP_AGENT_ROOT}")"
source "${BOOTSTRAP_AGENT_ROOT}/scripts/runtime_env.sh"
dpl_init_runtime "run_candidate_matrix.sh"

PLAN_FILE="${DPL_EVOLVE_AGENT_ROOT}/configs/experiment_plans/flow_knowledge_core_util.tsv"
MATRIX_ID=""
OUTPUT_ROOT=""
CANDIDATE_SRC=""
CANDIDATE_LABEL="candidate"
THREADS="10"
MAX_PARALLEL="1"
SKIP_PLACE=0
FORCE_PLACE=0
SKIP_BASELINE=0
BASELINE_ONLY=0
DRY_RUN=0

usage() {
  cat <<'EOF'
Usage: run_candidate_matrix.sh --matrix-id NAME --candidate-src PATH [options]

Build one fixed dpl_evolve source once, then evaluate that same code across a
TSV experiment plan.  This is the fixed-source replay verification path for knowledge
learned by Teacher/Student search.

Options:
  --matrix-id NAME       Stable output id under the candidate-matrix output root.
  --output-root PATH     Directory that contains per-matrix subdirectories.
                         Default: $DPL_EVOLVE_STATE_ROOT/candidate_matrices.
  --candidate-src PATH   Fixed dpl_evolve source tree to test, e.g. an exported
                         student source commit.
  --candidate-label NAME Short run-tag label. Default: candidate.
  --plan PATH            TSV plan. Default: configs/experiment_plans/flow_knowledge_core_util.tsv.
  --threads N            OpenROAD threads. Default: 10.
  --max-parallel N       Parallel plan rows. Default: 1. Use 9 for the
                         baseline-only full matrix on a machine with capacity.
  --skip-place           Reuse existing placement snapshots.
  --force-place          Regenerate placement snapshots even if present.
  --skip-baseline        Do not refresh canonical baselines.
  --baseline-only        Refresh snapshots/baselines but do not build/evaluate candidate.
  --dry-run              Print commands without running them.
  --help                 Show this message.

Plan columns:
  enabled, case, core_utilization, flow_variant, round_id, start_kind, notes

Only the case/core_utilization/flow_variant columns are used for candidate
evaluation.  The candidate source is identical for every enabled row.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --matrix-id) MATRIX_ID="$2"; shift 2 ;;
    --output-root) OUTPUT_ROOT="$2"; shift 2 ;;
    --candidate-src) CANDIDATE_SRC="$2"; shift 2 ;;
    --candidate-label) CANDIDATE_LABEL="$2"; shift 2 ;;
    --plan) PLAN_FILE="$2"; shift 2 ;;
    --threads) THREADS="$2"; shift 2 ;;
    --max-parallel) MAX_PARALLEL="$2"; shift 2 ;;
    --skip-place) SKIP_PLACE=1; shift ;;
    --force-place) FORCE_PLACE=1; shift ;;
    --skip-baseline) SKIP_BASELINE=1; shift ;;
    --baseline-only) BASELINE_ONLY=1; shift ;;
    --dry-run) DRY_RUN=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) echo "[ERROR] Unknown argument: $1" >&2; usage >&2; exit 1 ;;
  esac
done

if [[ -z "${MATRIX_ID}" ]]; then
  echo "[ERROR] Missing --matrix-id" >&2
  usage >&2
  exit 1
fi

if [[ "${BASELINE_ONLY}" -ne 1 ]]; then
  if [[ -z "${CANDIDATE_SRC}" ]]; then
    echo "[ERROR] Missing --candidate-src" >&2
    usage >&2
    exit 1
  fi
  CANDIDATE_SRC="$(realpath -m "${CANDIDATE_SRC}")"
  if [[ ! -f "${CANDIDATE_SRC}/CMakeLists.txt" ]]; then
    echo "[ERROR] candidate source is not a dpl_evolve tree: ${CANDIDATE_SRC}" >&2
    exit 1
  fi
fi

PLAN_FILE="$(realpath -m "${PLAN_FILE}")"
if [[ ! -f "${PLAN_FILE}" ]]; then
  echo "[ERROR] Missing plan file: ${PLAN_FILE}" >&2
  exit 1
fi

OUTPUT_ROOT="${OUTPUT_ROOT:-${DPL_EVOLVE_STATE_ROOT}/candidate_matrices}"
OUTPUT_ROOT="$(realpath -m "${OUTPUT_ROOT}")"
MATRIX_ROOT="${OUTPUT_ROOT}/${MATRIX_ID}"
MATRIX_LOG="${MATRIX_ROOT}/run.log"
RESULTS_TSV="${MATRIX_ROOT}/results.tsv"
ROW_STATUS_DIR="${MATRIX_ROOT}/row_status"
RESULTS_LOCK="${MATRIX_ROOT}/results.lock"
VARIANT_ROOT="${MATRIX_ROOT}/variant"
DPL_SRC="${VARIANT_ROOT}/dpl_evolve"
PRIVATE_BINARY="${VARIANT_ROOT}/install/OpenROAD/bin/openroad"

mkdir -p "${MATRIX_ROOT}"
rm -rf "${ROW_STATUS_DIR}"
mkdir -p "${ROW_STATUS_DIR}"
: > "${MATRIX_LOG}"
RESULTS_HEADER="case	core_utilization	flow_variant	baseline_tag	candidate_tag	status	exit_code	hpwl_after_micron	hpwl_delta_percent	hpwl_global_micron	hpwl_legalized_micron	hpwl_after_improve_micron	hpwl_delta_legalization_micron	hpwl_delta_improve_micron	hpwl_delta_final_micron	hpwl_delta_legalization_percent	hpwl_delta_improve_percent	hpwl_delta_final_percent	avg_displacement_micron	max_displacement_micron	runtime_seconds	candidate_metrics"
printf "%s\n" "${RESULTS_HEADER}" > "${RESULTS_TSV}"

log() {
  local msg="$*"
  printf '[candidate_matrix] %s\n' "${msg}" | tee -a "${MATRIX_LOG}"
}

run_cmd() {
  log "+ $(printf '%q ' "$@")"
  if [[ "${DRY_RUN}" -eq 1 ]]; then
    return 0
  fi
  "$@" 2>&1 | tee -a "${MATRIX_LOG}"
}

run_cmd_env() {
  local env_name="$1"
  local env_value="$2"
  shift 2
  log "+ env ${env_name}=$(printf '%q' "${env_value}") $(printf '%q ' "$@")"
  if [[ "${DRY_RUN}" -eq 1 ]]; then
    return 0
  fi
  env "${env_name}=${env_value}" "$@" 2>&1 | tee -a "${MATRIX_LOG}"
}

case_field() {
  "${DPL_EVOLVE_PYTHON}" "${DPL_EVOLVE_AGENT_ROOT}/scripts/repo/case_registry.py" --case "$1" --field "$2"
}

snapshot_path() {
  local case_id="$1"
  local flow_variant="$2"
  local platform design
  platform="$(case_field "${case_id}" platform)"
  design="$(case_field "${case_id}" design)"
  printf '%s\n' "${ORFS_ROOT}/flow/results/${platform}/${design}/${flow_variant}/3_4_place_resized.odb"
}

metrics_path() {
  local case_id="$1"
  local flow_variant="$2"
  local run_tag="$3"
  local platform design
  platform="$(case_field "${case_id}" platform)"
  design="$(case_field "${case_id}" design)"
  printf '%s\n' "${ORFS_ROOT}/flow/reports/${platform}/${design}/${flow_variant}/dpl_evolve_baseline/${run_tag}/metrics.json"
}

metric_fields() {
  local metrics_json="$1"
  local case_id="${2:-}"
  local flow_variant="${3:-}"
  local run_tag="${4:-}"
  local platform=""
  local design=""
  local legalize_log=""
  if [[ -n "${case_id}" && -n "${flow_variant}" && -n "${run_tag}" ]]; then
    platform="$(case_field "${case_id}" platform)"
    design="$(case_field "${case_id}" design)"
    legalize_log="${ORFS_ROOT}/flow/logs/${platform}/${design}/${flow_variant}/dpl_evolve_baseline/${run_tag}/dpl_evolve_${run_tag}_legalize.log"
  fi
  "${DPL_EVOLVE_PYTHON}" - "${metrics_json}" "${legalize_log}" <<'PY'
import json
import re
import sys
from pathlib import Path

path = Path(sys.argv[1])
log_path = Path(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[2] else None
values = [""] * 14
def as_float(value):
    try:
        if value in (None, ""):
            return None
        return float(value)
    except (TypeError, ValueError):
        return None

def stage_values(data, hpwl, log_path):
    stages = data.get("hpwl_stages") if isinstance(data, dict) else {}
    if not isinstance(stages, dict):
        stages = {}
    hpwlg = as_float(stages.get("global_micron")) or as_float(hpwl.get("before_micron"))
    legalized = as_float(stages.get("legalized_micron"))
    improved = as_float(stages.get("after_improve_micron"))
    final = as_float(stages.get("final_micron")) or as_float(hpwl.get("after_micron"))
    if log_path and log_path.is_file():
        text = log_path.read_text(encoding="utf-8", errors="replace")
        def first(pattern):
            m = re.search(pattern, text, flags=re.MULTILINE)
            return as_float(m.group(1)) if m else None
        def last(pattern):
            m = re.findall(pattern, text, flags=re.MULTILINE)
            return as_float(m[-1]) if m else None
        hpwlg = hpwlg or first(r"^original HPWL\s+([0-9.+-eE]+)\s+u") or first(r"^Original HPWL\s+([0-9.+-eE]+)\s+u")
        legalized = legalized or last(r"^legalized HPWL\s+([0-9.+-eE]+)\s+u")
        improved = improved or last(r"^Final HPWL\s+([0-9.+-eE]+)\s+u")
        final = final or last(r"^\[INFO DPL-0022\]\s+HPWL after\s+([0-9.+-eE]+)\s+u")
    d_lg = None if hpwlg is None or legalized is None else legalized - hpwlg
    d_ip = None if legalized is None or improved is None else improved - legalized
    d_final = None if hpwlg is None or final is None else final - hpwlg
    def pct(delta):
        return None if delta is None or hpwlg in (None, 0.0) else delta / hpwlg * 100.0
    return [hpwlg, legalized, improved, d_lg, d_ip, d_final, pct(d_lg), pct(d_ip), pct(d_final)]

def stringify(items):
    return ["" if item is None else str(item) for item in items]

if path.is_file():
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        hpwl = data.get("hpwl")
        if not isinstance(hpwl, dict) or hpwl.get("source") == "cell_bbox_proxy":
            hpwl = data.get("hpwl_openroad_log")
        if not isinstance(hpwl, dict):
            hpwl = {}
        disp = data.get("displacement", {})
        hpwl_delta_percent = hpwl.get("delta_percent", "")
        if not hpwl_delta_percent:
            try:
                before_value = float(hpwl.get("before_micron", ""))
                delta_value = float(hpwl.get("delta_micron", ""))
                if before_value != 0.0:
                    hpwl_delta_percent = str(delta_value / before_value * 100.0)
            except (TypeError, ValueError):
                pass
        stages = stage_values(data, hpwl, log_path)
        values = [
            hpwl.get("after_micron", ""),
            hpwl_delta_percent,
            *stringify(stages),
            disp.get("average_displacement_micron", ""),
            disp.get("max_displacement_micron", ""),
            data.get("runtime_seconds", ""),
        ]
    except Exception:
        pass
if not values[0] and log_path and log_path.is_file():
    text = log_path.read_text(encoding="utf-8", errors="replace")

    def last(pattern: str) -> str:
        matches = re.findall(pattern, text, flags=re.MULTILINE)
        return matches[-1] if matches else ""

    hpwl = (
        last(r"^\[INFO DPL-0022\]\s+HPWL after\s+([0-9.+-eE]+)\s+u")
        or last(r"^Final HPWL\s+([0-9.+-eE]+)\s+u")
        or last(r"^legalized HPWL\s+([0-9.+-eE]+)\s+u")
    )
    before = (
        last(r"^original HPWL\s+([0-9.+-eE]+)\s+u")
        or last(r"^Original HPWL\s+([0-9.+-eE]+)\s+u")
    )
    hpwl_delta_percent = ""
    try:
        before_value = float(before)
        after_value = float(hpwl)
        if before_value != 0.0:
            hpwl_delta_percent = str((after_value - before_value) / before_value * 100.0)
    except (TypeError, ValueError):
        pass
    avg = last(r"average displacement\s+([0-9.+-eE]+)\s+u")
    maxd = last(r"max displacement\s+([0-9.+-eE]+)\s+u")
    elapsed = last(r"Elapsed time:\s+([0-9:.]+)\[h:\]min:sec")
    runtime = ""
    if elapsed:
        parts = [float(part) for part in elapsed.split(":")]
        if len(parts) == 3:
            runtime = str(parts[0] * 3600 + parts[1] * 60 + parts[2])
        elif len(parts) == 2:
            runtime = str(parts[0] * 60 + parts[1])
        elif len(parts) == 1:
            runtime = str(parts[0])
    stages = stage_values({}, {"before_micron": before, "after_micron": hpwl}, log_path)
    values = [hpwl, values[1] or hpwl_delta_percent, *stringify(stages), values[11] or avg, values[12] or maxd, values[13] or runtime]
print("\t".join("" if value is None else str(value) for value in values))
PY
}

sanitize() {
  printf '%s\n' "$1" | tr -c 'A-Za-z0-9_-' '_'
}

finalize_results() {
  (
    flock 9
    printf "%s\n" "${RESULTS_HEADER}" > "${RESULTS_TSV}"
    if compgen -G "${ROW_STATUS_DIR}/*.tsv" > /dev/null; then
      find "${ROW_STATUS_DIR}" -maxdepth 1 -type f -name '*.tsv' -print0 \
        | sort -z \
        | xargs -0 cat >> "${RESULTS_TSV}"
    fi
  ) 9>"${RESULTS_LOCK}"
}

trap finalize_results EXIT

declare -a PLAN_ROWS=()
while IFS=$'\t' read -r enabled case_id core_util flow_variant round_id row_start_kind notes || [[ -n "${enabled:-}" ]]; do
  [[ -z "${enabled:-}" || "${enabled}" == \#* ]] && continue
  [[ "${enabled}" == "enabled" ]] && continue
  [[ "${enabled}" == "0" || "${enabled}" == "false" || "${enabled}" == "skip" ]] && continue
  [[ "${core_util:-}" == "-" || "${core_util:-}" == "default" ]] && core_util=""
  if [[ -z "${case_id:-}" || -z "${flow_variant:-}" ]]; then
    echo "[ERROR] Bad plan row in ${PLAN_FILE}" >&2
    exit 1
  fi
  PLAN_ROWS+=("${case_id}|${core_util}|${flow_variant}|${round_id:-}|${notes:-}")
done < "${PLAN_FILE}"

if [[ "${#PLAN_ROWS[@]}" -eq 0 ]]; then
  echo "[ERROR] No enabled rows in ${PLAN_FILE}" >&2
  exit 1
fi

log "matrix_root=${MATRIX_ROOT}"
log "plan=${PLAN_FILE}"
log "candidate_src=${CANDIDATE_SRC:-baseline_only}"
log "candidate_label=${CANDIDATE_LABEL}"
log "max_parallel=${MAX_PARALLEL}"

if [[ "${BASELINE_ONLY}" -ne 1 ]]; then
  run_cmd "${DPL_EVOLVE_AGENT_ROOT}/scripts/workspace/create_variant_start.sh" \
    --orfs-root "${ORFS_ROOT}" \
    --variant-root "${VARIANT_ROOT}" \
    --dpl-src "${DPL_SRC}" \
    --seed-src "${CANDIDATE_SRC}" \
    --force

  run_cmd "${DPL_EVOLVE_AGENT_ROOT}/scripts/workspace/configure_openroad_variant_relink.sh" \
    --variant-root "${VARIANT_ROOT}" \
    --dpl-src "${DPL_SRC}"

  run_cmd "${DPL_EVOLVE_PYTHON}" "${DPL_EVOLVE_AGENT_ROOT}/scripts/workspace/build_openroad_variant_relink.py" \
    --variant-root "${VARIANT_ROOT}" \
    --dpl-src "${DPL_SRC}" \
    --threads "${THREADS}"
fi

run_plan_row() {
  local row_index="$1"
  local row="$2"
  local case_id core_util flow_variant round_id notes snapshot baseline_tag
  local candidate_tag candidate_metrics status_file

  IFS='|' read -r case_id core_util flow_variant round_id notes <<< "${row}"
  snapshot="$(snapshot_path "${case_id}" "${flow_variant}")"
  baseline_tag="${MATRIX_ID}_${case_id}_baseline_probe"
  candidate_tag="${MATRIX_ID}_$(sanitize "${case_id}")_${core_util:-default}_${CANDIDATE_LABEL}"
  candidate_metrics="$(metrics_path "${case_id}" "${flow_variant}" "${candidate_tag}")"
  status_file="${ROW_STATUS_DIR}/$(printf '%03d' "${row_index}").tsv"

  record_status() {
    local status="$1"
    local exit_code="$2"
    local metrics_fields
    local tmp_file
    metrics_fields="$(metric_fields "${candidate_metrics}" "${case_id}" "${flow_variant}" "${candidate_tag}")"
    tmp_file="${status_file}.$$.${BASHPID}.tmp"
    printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
      "${case_id}" "${core_util:-default}" "${flow_variant}" \
      "${baseline_tag}" "${candidate_tag}" "${status}" "${exit_code}" \
      "${metrics_fields}" "${candidate_metrics}" \
      > "${tmp_file}"
    mv "${tmp_file}" "${status_file}"
    finalize_results
  }

  if [[ "${SKIP_PLACE}" -eq 1 ]]; then
    log "skip_place case=${case_id} flow_variant=${flow_variant}"
  elif [[ -f "${snapshot}" && "${FORCE_PLACE}" -ne 1 ]]; then
    log "reuse_snapshot case=${case_id} flow_variant=${flow_variant} snapshot=${snapshot}"
  elif [[ -n "${core_util}" ]]; then
    if run_cmd_env CORE_UTILIZATION "${core_util}" \
      "${DPL_EVOLVE_AGENT_ROOT}/scripts/evaluator/run_place_batch.sh" \
      --case "${case_id}" \
      --flow-variant "${flow_variant}" \
      --max-tasks 1 \
      --num-cores "${THREADS}" \
      --target place; then
      :
    else
      exit_code="$?"
      log "row_failed stage=place case=${case_id} exit_code=${exit_code}"
      record_status "FAIL_PLACE" "${exit_code}"
      return 0
    fi
  else
    if run_cmd "${DPL_EVOLVE_AGENT_ROOT}/scripts/evaluator/run_place_batch.sh" \
      --case "${case_id}" \
      --flow-variant "${flow_variant}" \
      --max-tasks 1 \
      --num-cores "${THREADS}" \
      --target place; then
      :
    else
      exit_code="$?"
      log "row_failed stage=place case=${case_id} exit_code=${exit_code}"
      record_status "FAIL_PLACE" "${exit_code}"
      return 0
    fi
  fi

  if [[ "${SKIP_BASELINE}" -ne 1 ]]; then
    if run_cmd "${DPL_EVOLVE_AGENT_ROOT}/baseline/run_baseline_suite.sh" \
      --case "${case_id}" \
      --flow-variant "${flow_variant}" \
      --threads "${THREADS}" \
      --tag-prefix "${baseline_tag}"; then
      :
    else
      exit_code="$?"
      log "row_failed stage=baseline case=${case_id} exit_code=${exit_code}"
      record_status "FAIL_BASELINE" "${exit_code}"
      return 0
    fi
  fi

  if [[ "${BASELINE_ONLY}" -eq 1 ]]; then
    candidate_tag=""
    candidate_metrics=""
    record_status "BASELINE_OK" "0"
    return 0
  fi

  if run_cmd "${DPL_EVOLVE_AGENT_ROOT}/baseline/run_baseline.sh" \
    --line evolve_default \
    --case "${case_id}" \
    --flow-variant "${flow_variant}" \
    --threads "${THREADS}" \
    --openroad-binary "${PRIVATE_BINARY}" \
    --run-tag "${candidate_tag}"; then
    :
  else
    exit_code="$?"
    log "row_failed stage=candidate case=${case_id} exit_code=${exit_code}"
    record_status "FAIL_CANDIDATE" "${exit_code}"
    return 0
  fi

  record_status "PASS" "0"
}

declare -a pids=()
failed=0

wait_for_slot() {
  while [[ "${#pids[@]}" -ge "${MAX_PARALLEL}" ]]; do
    if ! wait "${pids[0]}"; then
      failed=1
    fi
    pids=("${pids[@]:1}")
  done
}

row_index=0
for row in "${PLAN_ROWS[@]}"; do
  row_index=$((row_index + 1))
  wait_for_slot
  run_plan_row "${row_index}" "${row}" &
  pids+=("$!")
done

for pid in "${pids[@]}"; do
  if ! wait "${pid}"; then
    failed=1
  fi
done

finalize_results

if [[ "${failed}" -ne 0 ]]; then
  echo "[ERROR] One or more matrix rows failed. See ${MATRIX_LOG}" >&2
  log "failed_partial_results=${RESULTS_TSV}"
  exit 1
fi

log "done results=${RESULTS_TSV}"
