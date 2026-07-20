#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/../scripts/runtime_env.sh"
dpl_init_runtime "run_baseline_suite.sh"

WORKSPACE_ROOT="${ORFS_BUILD_ROOT:-${ORFS_ROOT}}"
FLOW_HOME="${WORKSPACE_ROOT}/flow"
FLOW_HOME="$(realpath -sm "${FLOW_HOME}")"
DEFAULT_CASE="gcd_nangate45"
export FLOW_HOME

usage() {
  cat <<'EOF'
Usage: run_baseline_suite.sh [options]

Runs the canonical strict comparison bundle:
  1. openroad_dpl_flow
  2. openroad_dpl_negotiation
  3. evolve_default

Options:
  --case ID                 Case id under problems/. Default: gcd_nangate45.
  --design-config PATH      Internal/debug override. Prefer --case.
  --flow-variant NAME       FLOW_VARIANT shared by all runs.
  --threads N               NUM_CORES passed to each run.
  --jobs N                  Number of baseline lines to run concurrently. Default: 3.
  --input-stage NAME        Input placement stage. Default: 3_4_place_resized.
  --tag-prefix NAME         Prefix added to every run tag.
  --openroad-binary PATH    Optional explicit OpenROAD binary.
  --help                    Show this message.
EOF
}

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

flow_abs() {
  local path="$1"
  if [[ "${path}" == /* ]]; then
    realpath -sm "${path}"
  else
    realpath -sm "${FLOW_HOME}/${path}"
  fi
}

shell_join() {
  local out=()
  local arg
  for arg in "$@"; do
    out+=("$(printf '%q' "${arg}")")
  done
  printf '%s' "${out[*]}"
}

design_config=""
case_id=""
flow_variant=""
threads=""
jobs="3"
input_stage="3_4_place_resized"
tag_prefix=""
openroad_binary=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --design-config) design_config="$2"; shift 2 ;;
    --case) case_id="$2"; shift 2 ;;
    --flow-variant) flow_variant="$2"; shift 2 ;;
    --threads) threads="$2"; shift 2 ;;
    --jobs) jobs="$2"; shift 2 ;;
    --input-stage) input_stage="$2"; shift 2 ;;
    --tag-prefix) tag_prefix="$2"; shift 2 ;;
    --openroad-binary) openroad_binary="$2"; shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 1 ;;
  esac
done

if [[ -z "${design_config}" ]]; then
  case_id="${case_id:-$DEFAULT_CASE}"
  design_config="$("${DPL_EVOLVE_PYTHON}" "${SCRIPT_DIR}/../scripts/repo/case_registry.py" --case "${case_id}" --field design_config)"
fi
if [[ "${design_config}" == /* ]]; then
  design_config="$(to_flow_relative_if_possible "${design_config}")"
else
  design_config="$(normalize_flow_relpath "${design_config}")"
fi

flow_variant="${flow_variant:-dpl_evolve_suite_$(date +%Y%m%d_%H%M%S)}"
flow_variant="$(normalize_flow_relpath "${flow_variant}")"

tag_prefix="${tag_prefix#./}"
if [[ -n "${tag_prefix}" && "${tag_prefix}" != *_ ]]; then
  tag_prefix="${tag_prefix}_"
fi

make_base=(make -C "${FLOW_HOME}" --no-print-directory DESIGN_CONFIG="${design_config}" FLOW_VARIANT="${flow_variant}")
if [[ -n "${threads}" ]]; then
  make_base+=(NUM_CORES="${threads}")
fi
if ! [[ "${jobs}" =~ ^[0-9]+$ ]] || [[ "${jobs}" -lt 1 ]]; then
  echo "--jobs must be a positive integer, got: ${jobs}" >&2
  exit 1
fi

print_make_var() {
  local var="$1"
  "${make_base[@]}" "print-${var}" | sed -n "s/^${var}: //p" | tail -n 1
}

reports_dir="$(normalize_flow_relpath "$(print_make_var REPORTS_DIR)")"
results_dir="$(normalize_flow_relpath "$(print_make_var RESULTS_DIR)")"
report_root="${reports_dir}/dpl_evolve_baseline"
suite_summary_tsv="$(flow_abs "${report_root}/suite_runs.tsv")"
mkdir -p "$(dirname "${suite_summary_tsv}")"

input_snapshot_rel="${results_dir}/${input_stage}.odb"
input_snapshot_abs="$(flow_abs "${input_snapshot_rel}")"
if [[ -f "${input_snapshot_abs}" ]]; then
  echo "Using existing input snapshot ${input_snapshot_rel} for ${flow_variant}."
else
  echo "Preparing input snapshot ${input_stage} for ${flow_variant} before parallel baseline runs..."
  "${make_base[@]}" "${input_stage}"
fi

common_args=(
  --design-config "${design_config}"
  --input-stage "${input_stage}"
  --flow-variant "${flow_variant}"
)
if [[ -n "${threads}" ]]; then
  common_args+=(--threads "${threads}")
fi
if [[ -n "${openroad_binary}" ]]; then
  common_args+=(--openroad-binary "${openroad_binary}")
fi

run_case() {
  local line_id="$1"
  local run_tag="$2"
  local runner="$3"
  shift 3

  local -a cmd=("${runner}" "${common_args[@]}" --run-tag "${run_tag}" "$@")

  echo
  echo "==> Running ${line_id}"
  echo "    $(shell_join "${cmd[@]}")"
  "${cmd[@]}"
}

write_suite_summary() {
  cat > "${suite_summary_tsv}" <<'EOF'
line_id	run_tag	metrics_json
EOF
  local metrics_json
  local i
  for i in "${!suite_line_ids[@]}"; do
    metrics_json="$(flow_abs "${report_root}/${suite_run_tags[$i]}/metrics.json")"
    printf '%s\t%s\t%s\n' "${suite_line_ids[$i]}" "${suite_run_tags[$i]}" "${metrics_json}" >> "${suite_summary_tsv}"
  done
}

suite_line_ids=(
  "openroad_dpl_flow"
  "openroad_dpl_negotiation"
  "evolve_default"
)
suite_run_tags=(
  "${tag_prefix}openroad_dpl_flow"
  "${tag_prefix}openroad_dpl_negotiation"
  "${tag_prefix}evolve_default"
)
suite_runners=(
  "${SCRIPT_DIR}/run_openroad_dpl_flow.sh"
  "${SCRIPT_DIR}/run_openroad_dpl_negotiation.sh"
  "${SCRIPT_DIR}/run_evolve_default.sh"
)

failure=0
if [[ "${jobs}" -le 1 ]]; then
  for i in "${!suite_line_ids[@]}"; do
    run_case "${suite_line_ids[$i]}" "${suite_run_tags[$i]}" "${suite_runners[$i]}" || failure=1
  done
else
  active=0
  for i in "${!suite_line_ids[@]}"; do
    run_case "${suite_line_ids[$i]}" "${suite_run_tags[$i]}" "${suite_runners[$i]}" &
    active=$((active + 1))
    if [[ "${active}" -ge "${jobs}" ]]; then
      wait -n || failure=1
      active=$((active - 1))
    fi
  done
  while [[ "${active}" -gt 0 ]]; do
    wait -n || failure=1
    active=$((active - 1))
  done
fi

write_suite_summary

echo
echo "Suite complete."
echo "  flow_variant: ${flow_variant}"
echo "  summary_tsv: ${suite_summary_tsv}"
cat "${suite_summary_tsv}"

exit "${failure}"
