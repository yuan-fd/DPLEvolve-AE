#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/../scripts/runtime_env.sh"
dpl_init_runtime "run_baseline.sh"

WORKSPACE_ROOT="${ORFS_BUILD_ROOT:-${ORFS_ROOT}}"
FLOW_DIR="${WORKSPACE_ROOT}/flow"
FLOW_HOME="${FLOW_DIR}"
FLOW_HOME="$(realpath -sm "${FLOW_HOME}")"
DEFAULT_CASE="gcd_nangate45"
export FLOW_HOME

usage() {
  cat <<'EOF'
Usage: run_baseline.sh --line NAME [options]

Canonical strict-only lines:
  openroad_dpl_flow          OpenROAD detailed_placement, improve_placement, optimize_mirroring.
  openroad_dpl_negotiation   OpenROAD detailed_placement -use_negotiation, improve_placement, optimize_mirroring.
  evolve_default             detailed_placement_evolve, improve_placement_evolve, optimize_mirroring_evolve.

Options:
  --line NAME                Canonical line to run.
  --case ID                  Case id under problems/. Default: gcd_nangate45.
  --design-config PATH       Internal/debug override. Prefer --case.
  --input-stage NAME         Input placement stage. Default: 3_4_place_resized.
  --flow-variant NAME        Existing FLOW_VARIANT containing the input snapshot.
  --run-tag NAME             Stable tag under dpl_evolve_baseline/.
  --threads N                NUM_CORES passed to ORFS/OpenROAD.
  --openroad-binary PATH     Explicit OpenROAD binary override.
  --legalize-timeout-seconds N
                             Optional timeout applied only to the OpenROAD
                             legalize/improve/mirror flow run. Metrics
                             collection is not timeout-wrapped.
  --help                     Show this message.

This dispatcher intentionally has no flow track and no engine switch.  The
canonical line name is the source of truth for the command that is run.
EOF
}

line=""
design_config=""
case_id=""
input_stage="3_4_place_resized"
flow_variant=""
run_tag=""
threads=""
openroad_binary=""
legalize_timeout_seconds=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --line)
      line="$2"
      shift 2
      ;;
    --design-config)
      design_config="$2"
      shift 2
      ;;
    --case)
      case_id="$2"
      shift 2
      ;;
    --input-stage)
      input_stage="$2"
      shift 2
      ;;
    --flow-variant)
      flow_variant="$2"
      shift 2
      ;;
    --run-tag)
      run_tag="$2"
      shift 2
      ;;
    --threads)
      threads="$2"
      shift 2
      ;;
    --openroad-binary)
      openroad_binary="$2"
      shift 2
      ;;
    --legalize-timeout-seconds)
      legalize_timeout_seconds="$2"
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ -z "${line}" ]]; then
  echo "Missing required --line." >&2
  usage >&2
  exit 1
fi

case "${line}" in
  openroad_dpl_flow|openroad_dpl_negotiation|evolve_default) ;;
  *)
    echo "Unsupported canonical line: ${line}" >&2
    usage >&2
    exit 1
    ;;
esac

if [[ -n "${openroad_binary}" ]]; then
  export OPENROAD_EXE="$(realpath -sm "${openroad_binary}")"
  if [[ ! -x "${OPENROAD_EXE}" ]]; then
    echo "[ERROR] OpenROAD binary is not executable: ${OPENROAD_EXE}" >&2
    exit 1
  fi
fi

normalize_flow_relpath() {
  local path="$1"
  if [[ -z "${path}" ]]; then
    printf '\n'
    return
  fi
  if [[ "${path}" == "${FLOW_HOME}/"* ]]; then
    printf '%s\n' "${path#${FLOW_HOME}/}"
    return
  fi
  if [[ "${path}" == /* ]]; then
    printf '%s\n' "${path}"
    return
  fi
  path="${path#./}"
  printf '%s\n' "${path}"
}

flow_abs() {
  local path="$1"
  if [[ "${path}" == /* ]]; then
    realpath -sm "${path}"
  else
    realpath -sm "${FLOW_HOME}/${path}"
  fi
}

to_flow_relative_if_possible() {
  local path="$1"
  local abs_path
  abs_path="$(realpath -sm "${path}")"
  if [[ "${abs_path}" == "${FLOW_HOME}/"* ]]; then
    printf '%s\n' "${abs_path#${FLOW_HOME}/}"
  else
    printf '%s\n' "${path}"
  fi
}

shell_join() {
  local parts=()
  local quoted
  for arg in "$@"; do
    printf -v quoted '%q' "$arg"
    parts+=("${quoted}")
  done
  printf '%s ' "${parts[@]}"
}

write_tcl_driver() {
  local wrapper_path="$1"
  local target_script="$2"
  shift 2
  {
    echo "set ::dpl_evolve_options [dict create \\"
    while [[ $# -gt 0 ]]; do
      printf '  %s {%s} \\\n' "$1" "$2"
      shift 2
    done
    echo "]"
    printf 'source {%s}\n' "${target_script}"
  } > "${wrapper_path}"
}

if [[ -z "${design_config}" ]]; then
  case_id="${case_id:-$DEFAULT_CASE}"
  design_config="$("${DPL_EVOLVE_PYTHON}" "${SCRIPT_DIR}/../scripts/repo/case_registry.py" --case "${case_id}" --field design_config)"
fi
if [[ "${design_config}" == /* ]]; then
  design_config="$(to_flow_relative_if_possible "${design_config}")"
else
  design_config="$(normalize_flow_relpath "${design_config}")"
fi

flow_variant="${flow_variant:-dpl_evolve_baseline_$(date +%Y%m%d_%H%M%S)}"
flow_variant="$(normalize_flow_relpath "${flow_variant}")"
run_tag="${run_tag:-$(date +%Y%m%d_%H%M%S)_${line}}"

case "${line}" in
  openroad_dpl_flow)
    manifest_engine="openroad_dpl_flow"
    command_set="classic"
    legalizer_mode="openroad_dpl_flow"
    detail_tcl="${SCRIPT_DIR}/detail_place_openroad.tcl"
    check_cmd="check_placement"
    ;;
  openroad_dpl_negotiation)
    manifest_engine="openroad_dpl_negotiation"
    command_set="classic_negotiation"
    legalizer_mode="openroad_dpl_negotiation"
    detail_tcl="${SCRIPT_DIR}/detail_place_openroad_negotiation.tcl"
    check_cmd="check_placement"
    ;;
  evolve_default)
    manifest_engine="evolve_default"
    command_set="evolve"
    legalizer_mode="evolve_default"
    detail_tcl="${SCRIPT_DIR}/detail_place_evolve.tcl"
    check_cmd="check_placement_evolve"
    ;;
esac

make_base=(make -C "${FLOW_HOME}" --no-print-directory DESIGN_CONFIG="${design_config}" FLOW_VARIANT="${flow_variant}")
if [[ -n "${threads}" ]]; then
  make_base+=(NUM_CORES="${threads}")
fi

print_make_var() {
  local var="$1"
  "${make_base[@]}" "print-${var}" | sed -n "s/^${var}: //p" | tail -n 1
}

results_dir="$(normalize_flow_relpath "$(print_make_var RESULTS_DIR)")"
reports_dir="$(normalize_flow_relpath "$(print_make_var REPORTS_DIR)")"
log_dir="$(normalize_flow_relpath "$(print_make_var LOG_DIR)")"
objects_dir="$(normalize_flow_relpath "$(print_make_var OBJECTS_DIR)")"

run_dir="${results_dir}/dpl_evolve_baseline/${run_tag}"
report_dir="${reports_dir}/dpl_evolve_baseline/${run_tag}"
run_log_dir="${log_dir}/dpl_evolve_baseline/${run_tag}"
run_objects_dir="${objects_dir}/dpl_evolve_baseline/${run_tag}"
mkdir -p \
  "$(flow_abs "${run_dir}")" \
  "$(flow_abs "${report_dir}")" \
  "$(flow_abs "${run_log_dir}")" \
  "$(flow_abs "${run_objects_dir}")"

manifest_json="${report_dir}/run_manifest.json"
cat > "$(flow_abs "${manifest_json}")" <<EOF
{
  "line": "${line}",
  "track": "strict",
  "engine": "${manifest_engine}",
  "command_set": "${command_set}",
  "design_config": "${design_config}",
  "flow_variant": "${flow_variant}",
  "input_stage": "${input_stage}",
  "run_tag": "${run_tag}",
  "threads": ${threads:-0},
  "flow_home": "${FLOW_HOME}",
  "results_dir": "${results_dir}",
  "reports_dir": "${reports_dir}",
  "log_dir": "${log_dir}",
  "legalize_timeout_seconds": ${legalize_timeout_seconds:-null}
}
EOF

legalize_make_base=(
  "${make_base[@]}"
  RESULTS_DIR="$(flow_abs "${results_dir}")"
  REPORTS_DIR="$(flow_abs "${reports_dir}")"
  LOG_DIR="$(flow_abs "${run_log_dir}")"
  OBJECTS_DIR="$(flow_abs "${run_objects_dir}")"
)

metrics_make_base=(
  "${make_base[@]}"
  RESULTS_DIR="$(flow_abs "${run_dir}")"
  REPORTS_DIR="$(flow_abs "${report_dir}")"
  LOG_DIR="$(flow_abs "${run_log_dir}")"
  OBJECTS_DIR="$(flow_abs "${run_objects_dir}")"
)

input_snapshot_rel="${results_dir}/${input_stage}.odb"
input_snapshot_abs="$(flow_abs "${input_snapshot_rel}")"
if [[ -f "${input_snapshot_abs}" ]]; then
  echo "Using existing input snapshot ${input_snapshot_rel} for ${flow_variant}."
else
  echo "Preparing input snapshot ${input_stage} for ${flow_variant}..."
  "${make_base[@]}" "${input_stage}"
fi

# Freeze the exact protected evaluator, input ODB, and candidate binary before
# OpenROAD starts.  Verification below runs after placement and before metrics
# are assembled, so a changed input/evaluator can never produce an eligible
# fresh result.
run_provenance_cmd=(
  "${DPL_EVOLVE_PYTHON}"
  "${SCRIPT_DIR}/../scripts/evaluator/run_provenance.py"
  --manifest "$(flow_abs "${manifest_json}")"
  --input-snapshot "${input_snapshot_abs}"
  --protected-file "${BASH_SOURCE[0]}"
  --protected-file "${detail_tcl}"
  --protected-file "${SCRIPT_DIR}/collect_metrics.py"
  --protected-file "${SCRIPT_DIR}/report_legalized_metrics.tcl"
)
if [[ -n "${OPENROAD_EXE:-}" ]]; then
  run_provenance_cmd+=(--openroad-binary "${OPENROAD_EXE}")
fi
"${run_provenance_cmd[@]}"

before_snapshot="${run_dir}/before.tsv"
after_snapshot="${run_dir}/after.tsv"
legalize_summary_json="${report_dir}/legalize_summary.json"
post_metrics_summary_json="${report_dir}/post_metrics_summary.json"
metadata_json="${report_dir}/metadata.json"
metrics_json="${report_dir}/metrics.json"
dp_report="${report_dir}/detailed_placement_report.json"
check_report="${report_dir}/check_placement_report.json"
output_odb="${run_dir}/legalized.odb"
metrics_input_sdc="../../2_floorplan.sdc"
stage_metrics_json="${run_log_dir}/3_5_place_dp.json"
legalize_log_stem="dpl_evolve_${run_tag}_legalize"
strict_driver="${run_objects_dir}/${run_tag}_legalize_driver.tcl"

write_tcl_driver "$(flow_abs "${strict_driver}")" "$(flow_abs "${detail_tcl}")" \
  input-odb "${input_stage}.odb" \
  input-sdc "2_floorplan.sdc" \
  run-dir "${run_dir}" \
  report-dir "${report_dir}" \
  before-snapshot "${before_snapshot}" \
  after-snapshot "${after_snapshot}" \
  output-odb "${output_odb}" \
  summary-json "${legalize_summary_json}" \
  dp-report "${dp_report}" \
  track "strict"

start_ns="$(date +%s%N)"
legalize_cmd=(
  "${legalize_make_base[@]}"
  run \
  RUN_SCRIPT="$(flow_abs "${strict_driver}")" \
  RUN_LOG_NAME_STEM="${legalize_log_stem}"
)
set +e
if [[ -n "${legalize_timeout_seconds}" ]]; then
  timeout --kill-after=30s "${legalize_timeout_seconds}s" "${legalize_cmd[@]}"
  legalize_status=$?
else
  "${legalize_cmd[@]}"
  legalize_status=$?
fi
set -e
end_ns="$(date +%s%N)"
runtime_sec="$(awk "BEGIN {printf \"%.3f\", (${end_ns} - ${start_ns}) / 1000000000}")"
legalize_failure_reason=""
if [[ "${legalize_status}" -eq 124 || "${legalize_status}" -eq 137 ]]; then
  legalize_failure_reason="flow_timeout"
elif [[ "${legalize_status}" -ne 0 ]]; then
  legalize_failure_reason="flow_failed"
fi

metrics_driver="${run_objects_dir}/${run_tag}_metrics_driver.tcl"
write_tcl_driver "$(flow_abs "${metrics_driver}")" "$(flow_abs "${SCRIPT_DIR}/report_legalized_metrics.tcl")" \
  input-odb "legalized.odb" \
  input-sdc "${metrics_input_sdc}" \
  check-cmd "${check_cmd}" \
  check-report "${check_report}" \
  summary-json "${post_metrics_summary_json}" \
  metrics-json "${stage_metrics_json}" \
  track "strict" \
  legalizer "${legalizer_mode}"
if [[ "${legalize_status}" -eq 0 ]]; then
  metrics_openroad_args="$(shell_join -metrics "$(flow_abs "${stage_metrics_json}")")"
  "${metrics_make_base[@]}" \
    run \
    RUN_SCRIPT="$(flow_abs "${metrics_driver}")" \
    RUN_LOG_NAME_STEM="3_5_place_dp" \
    RUN_OPENROAD_ARGS="${metrics_openroad_args}"

  FLOW_HOME="${FLOW_HOME}" "${DPL_EVOLVE_PYTHON}" "${SCRIPT_DIR}/gen_orfs_metadata.py" \
    --orfs-metrics "${stage_metrics_json}" \
    --output "${metadata_json}"
else
  echo "[WARN] Legalize flow failed with status ${legalize_status}; collecting failure metrics without running post-legalization evaluator." >&2
  failure_json="$(flow_abs "${report_dir}/legalize_failure.json")"
  FLOW_HOME="${FLOW_HOME}" "${DPL_EVOLVE_PYTHON}" - "${failure_json}" "${legalize_status}" "${legalize_failure_reason}" "${legalize_timeout_seconds:-}" "${run_log_dir}/${legalize_log_stem}.log" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
payload = {
    "status": sys.argv[3],
    "legalize_exit_status": int(sys.argv[2]),
    "legalize_timeout_seconds": int(sys.argv[4]) if sys.argv[4] else None,
    "legalize_log": sys.argv[5],
}
path.parent.mkdir(parents=True, exist_ok=True)
path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
PY
fi

"${DPL_EVOLVE_PYTHON}" "${SCRIPT_DIR}/../scripts/evaluator/run_provenance.py" \
  --manifest "$(flow_abs "${manifest_json}")" \
  --verify

collect_args=(
  --before "${before_snapshot}"
  --after "${after_snapshot}"
  --run-summary "${legalize_summary_json}"
  --post-metrics-summary "${post_metrics_summary_json}"
  --manifest "${manifest_json}"
  --detailed-placement-report "${dp_report}"
  --legalize-log "${run_log_dir}/${legalize_log_stem}.log"
  --orfs-metrics "${stage_metrics_json}"
  --orfs-metadata "${metadata_json}"
  --runtime-seconds "${runtime_sec}"
  --run-status "$([[ "${legalize_status}" -eq 0 ]] && printf ok || printf '%s' "${legalize_failure_reason}")"
  --legalize-exit-status "${legalize_status}"
  --legalize-timeout-seconds "${legalize_timeout_seconds:-0}"
  --output "${metrics_json}"
)
FLOW_HOME="${FLOW_HOME}" "${DPL_EVOLVE_PYTHON}" "${SCRIPT_DIR}/collect_metrics.py" "${collect_args[@]}"

if [[ "${legalize_status}" -ne 0 ]]; then
  echo "[ERROR] Strict baseline flow failed." >&2
  echo "  line: ${line}" >&2
  echo "  reason: ${legalize_failure_reason}" >&2
  echo "  status: ${legalize_status}" >&2
  echo "  metrics: ${metrics_json}" >&2
  exit "${legalize_status}"
fi

echo "Strict baseline complete."
echo "  line: ${line}"
echo "  run_dir: ${run_dir}"
echo "  report_dir: ${report_dir}"
echo "  metrics: ${metrics_json}"
