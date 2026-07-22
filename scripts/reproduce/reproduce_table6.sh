#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

THREADS="${THREADS:-10}"
CHECK_ONLY=0
ONLY_ROLE=""
ONLY_CASE=""
ONLY_PATTERN=""
PLAN="${AE_ROOT}/configs/reproduction/table6-replay.tsv"

usage() {
  cat <<'EOF'
Usage: reproduce_table6.sh [options]

Build the retained evolved-negotiation source once, then execute Diamond,
Negotiation, and ReviewDSE on each of the nine exact cut-row DEF/V/SDC inputs.
Each legalizer has the paper's 7200-second cap.  No paper-time ODB is needed.

Options:
  --threads N              OpenROAD/build threads. Default: 10.
  --case CASE              Run one data case (for example ariane133_placebatch).
  --pattern PATTERN        Run one pattern; requires --case.
  --only-role ROLE         diamond, negotiation, or reviewdse.
  --check-paper-data       Verify the external package and exit.
  --check-inputs           Compatibility alias for --check-paper-data.
  --dry-run                Print commands without executing them.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --threads) THREADS="$2"; shift 2 ;;
    --case) ONLY_CASE="$2"; shift 2 ;;
    --pattern) ONLY_PATTERN="$2"; shift 2 ;;
    --only-role) ONLY_ROLE="$2"; shift 2 ;;
    --check-paper-data|--check-inputs) CHECK_ONLY=1; shift ;;
    --dry-run) REPRO_DRY_RUN=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) repro_die "unknown argument: $1" ;;
  esac
done
repro_positive_integer threads "${THREADS}"
if [[ -n "${ONLY_ROLE}" && "${ONLY_ROLE}" != diamond && "${ONLY_ROLE}" != negotiation && "${ONLY_ROLE}" != reviewdse ]]; then
  repro_die "--only-role must be diamond, negotiation, or reviewdse"
fi
if [[ -n "${ONLY_PATTERN}" && -z "${ONLY_CASE}" ]]; then
  repro_die "--pattern requires --case"
fi

missing=0
for path in \
  "${PAPER_DATA_ROOT}/table6/MANIFEST.sha256" \
  "${PAPER_DATA_ROOT}/table6/programs/evolved_negotiation/dpl_evolve/CMakeLists.txt"; do
  if [[ ! -f "${path}" ]]; then printf '[MISSING] %s\n' "${path}" >&2; missing=$((missing + 1)); fi
done
while IFS=$'\t' read -r enabled case_id pattern _rest; do
  [[ "${enabled}" == enabled || "${enabled}" == 0 || -z "${enabled}" ]] && continue
  for path in \
    "${PAPER_DATA_ROOT}/table6/cases/${case_id}/handoff.sdc" \
    "${PAPER_DATA_ROOT}/table6/cases/${case_id}/${pattern}/cutrows.def.gz" \
    "${PAPER_DATA_ROOT}/table6/cases/${case_id}/${pattern}/cutrows.v.gz"; do
    if [[ ! -f "${path}" ]]; then printf '[MISSING] %s\n' "${path}" >&2; missing=$((missing + 1)); fi
  done
done < "${PLAN}"
if [[ "${missing}" -gt 0 ]]; then
  echo "[BLOCKED] Table 6 needs the retained DEF/V/SDC + evolved-source data package; see docs/paper-data-layout.md" >&2
  exit 3
fi
if ! "${DPL_EVOLVE_PYTHON}" "${SCRIPT_DIR}/verify_data_manifest.py" \
  --root "${PAPER_DATA_ROOT}" --scope table6; then
  echo "[BLOCKED] Table 6 paper-data checksum verification failed" >&2
  exit 3
fi
if [[ "${CHECK_ONLY}" -eq 1 ]]; then
  echo "[PASS] exact Table 6 DEF/V/SDC inputs and single evolved source are present"
  exit 0
fi
if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then repro_require_runtime; fi

RUN_ID="table6_$(date +%Y%m%d_%H%M%S)"
RUN_ROOT="${REPRO_OUTPUT_ROOT}/${RUN_ID}"
RESULTS="${RUN_ROOT}/results.tsv"
VARIANT_ROOT="${DPL_EVOLVE_STATE_ROOT}/paper_reproduction/table6_evolved_negotiation_variant"
VARIANT_SRC="${VARIANT_ROOT}/dpl_evolve"
if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then
  mkdir -p "${RUN_ROOT}"
  printf 'case\tpattern\trole\tstatus\texit_code\truntime_seconds\tmetrics_json\tlog\n' > "${RESULTS}"
fi

if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then
  openroad_head="$(git -C "${ORFS_ROOT}/tools/OpenROAD" rev-parse --short HEAD)"
  BASE_OPENROAD="${DPL_EVOLVE_STATE_ROOT}/openroad_core/${openroad_head}/install/OpenROAD/bin/openroad"
  [[ -x "${BASE_OPENROAD}" ]] || repro_die "pinned OpenROAD binary missing: ${BASE_OPENROAD}; run 'make build-tools'"
else
  BASE_OPENROAD="${DPL_EVOLVE_STATE_ROOT}/openroad_core/<pinned>/install/OpenROAD/bin/openroad"
fi

if [[ -z "${ONLY_ROLE}" || "${ONLY_ROLE}" == reviewdse ]]; then
  repro_run "${DPL_EVOLVE_AGENT_ROOT}/scripts/workspace/create_variant_start.sh" \
    --orfs-root "${ORFS_ROOT}" \
    --variant-root "${VARIANT_ROOT}" \
    --dpl-src "${VARIANT_SRC}" \
    --seed-src "${PAPER_DATA_ROOT}/table6/programs/evolved_negotiation/dpl_evolve" \
    --git-branch paper/table6-evolved-negotiation \
    --force
  repro_run "${DPL_EVOLVE_AGENT_ROOT}/scripts/workspace/configure_openroad_variant_relink.sh" \
    --variant-root "${VARIANT_ROOT}" --dpl-src "${VARIANT_SRC}"
  repro_run "${DPL_EVOLVE_PYTHON}" \
    "${DPL_EVOLVE_AGENT_ROOT}/scripts/workspace/build_openroad_variant_relink.py" \
    --variant-root "${VARIANT_ROOT}" --dpl-src "${VARIANT_SRC}" --threads "${THREADS}"
fi
EVOLVED_OPENROAD="${VARIANT_ROOT}/install/OpenROAD/bin/openroad"

expand_platform_files() {
  local directory="$1" encoded="$2" output="" item
  local items=()
  IFS=';' read -r -a items <<< "${encoded}"
  for item in "${items[@]}"; do
    [[ -z "${item}" ]] && continue
    output+="${directory}/${item} "
  done
  printf '%s' "${output% }"
}

run_one() {
  local case_id="$1" pattern="$2" design="$3" nickname="$4"
  local lefs_encoded="$5" libs_encoded="$6" timeout_seconds="$7" role="$8"
  local binary eval_dir stage_dir log metrics status rc runtime additional_lefs lib_files
  stage_dir="${RUN_ROOT}/inputs/${case_id}/${pattern}"
  eval_dir="${RUN_ROOT}/runs/${case_id}/${pattern}/${role}"
  log="${eval_dir}/openroad.log"
  metrics="${eval_dir}/metrics.json"
  additional_lefs="$(expand_platform_files "${ORFS_ROOT}/flow/platforms/nangate45/lef" "${lefs_encoded}")"
  lib_files="$(expand_platform_files "${ORFS_ROOT}/flow/platforms/nangate45/lib" "${libs_encoded}")"
  if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then
    mkdir -p "${stage_dir}" "${eval_dir}"
    gzip -dc "${PAPER_DATA_ROOT}/table6/cases/${case_id}/${pattern}/cutrows.def.gz" > "${stage_dir}/cutrows.def"
    gzip -dc "${PAPER_DATA_ROOT}/table6/cases/${case_id}/${pattern}/cutrows.v.gz" > "${stage_dir}/cutrows.v"
  else
    repro_note "would decompress ${case_id}/${pattern} cutrows.def.gz and cutrows.v.gz"
  fi
  if [[ "${role}" == reviewdse ]]; then binary="${EVOLVED_OPENROAD}"; else binary="${BASE_OPENROAD}"; fi
  repro_shell_join timeout "${timeout_seconds}" env \
    FLOW_HOME="${ORFS_ROOT}/flow" \
    PLATFORM_DIR="${ORFS_ROOT}/flow/platforms/nangate45" \
    CUTROW_DEF="${stage_dir}/cutrows.def" \
    CUTROW_VERILOG="${stage_dir}/cutrows.v" \
    INPUT_SDC="${PAPER_DATA_ROOT}/table6/cases/${case_id}/handoff.sdc" \
    EVAL_DIR="${eval_dir}" METRICS_JSON="${metrics}" LEGALIZER_LINE="${role}" \
    CUTROW_CASE_ID="${case_id}" CUTROW_PATTERN_ID="${pattern}" \
    DESIGN_NAME="${design}" DESIGN_NICKNAME="${nickname}" PLATFORM=nangate45 \
    ADDITIONAL_LEFS="${additional_lefs}" LIB_FILES="${lib_files}" \
    "${binary}" -exit -no_init -threads "${THREADS}" -no_splash \
    "${SCRIPT_DIR}/openroad_legalize_cutrow.tcl"
  if [[ "${REPRO_DRY_RUN}" -eq 1 ]]; then return; fi
  set +e
  timeout "${timeout_seconds}" env \
    FLOW_HOME="${ORFS_ROOT}/flow" \
    PLATFORM_DIR="${ORFS_ROOT}/flow/platforms/nangate45" \
    CUTROW_DEF="${stage_dir}/cutrows.def" \
    CUTROW_VERILOG="${stage_dir}/cutrows.v" \
    INPUT_SDC="${PAPER_DATA_ROOT}/table6/cases/${case_id}/handoff.sdc" \
    EVAL_DIR="${eval_dir}" METRICS_JSON="${metrics}" LEGALIZER_LINE="${role}" \
    CUTROW_CASE_ID="${case_id}" CUTROW_PATTERN_ID="${pattern}" \
    DESIGN_NAME="${design}" DESIGN_NICKNAME="${nickname}" PLATFORM=nangate45 \
    ADDITIONAL_LEFS="${additional_lefs}" LIB_FILES="${lib_files}" \
    "${binary}" -exit -no_init -threads "${THREADS}" -no_splash \
    "${SCRIPT_DIR}/openroad_legalize_cutrow.tcl" > "${log}" 2>&1
  rc=$?
  set -e
  if [[ "${rc}" -eq 124 ]]; then
    status=timeout
    runtime="${timeout_seconds}"
  elif [[ -f "${metrics}" ]]; then
    read -r status runtime < <("${DPL_EVOLVE_PYTHON}" -c \
      'import json,sys; d=json.load(open(sys.argv[1])); print(d.get("status","invalid"), d.get("runtime_seconds",""))' "${metrics}")
    [[ "${status}" == ok ]] && status=pass || status=fail
  else
    status=fail
    runtime=""
  fi
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "${case_id}" "${pattern}" "${role}" "${status}" "${rc}" "${runtime}" "${metrics}" "${log}" >> "${RESULTS}"
  repro_note "Table 6 ${case_id}/${pattern}/${role}: ${status}"
}

while IFS=$'\t' read -r enabled case_id pattern design nickname lefs libs timeout_seconds; do
  [[ "${enabled}" == enabled || "${enabled}" == 0 || -z "${enabled}" ]] && continue
  [[ -n "${ONLY_CASE}" && "${case_id}" != "${ONLY_CASE}" ]] && continue
  [[ -n "${ONLY_PATTERN}" && "${pattern}" != "${ONLY_PATTERN}" ]] && continue
  for role in diamond negotiation reviewdse; do
    [[ -n "${ONLY_ROLE}" && "${role}" != "${ONLY_ROLE}" ]] && continue
    run_one "${case_id}" "${pattern}" "${design}" "${nickname}" "${lefs}" "${libs}" "${timeout_seconds}" "${role}"
  done
done < "${PLAN}"

if [[ "${REPRO_DRY_RUN}" -eq 0 && -z "${ONLY_ROLE}" && -z "${ONLY_CASE}" && -z "${ONLY_PATTERN}" ]]; then
  repro_run "${DPL_EVOLVE_PYTHON}" "${SCRIPT_DIR}/summarize_table6.py" \
    --results "${RESULTS}" \
    --experiment-manifest "${AE_ROOT}/configs/reproduction/paper-experiments.json" \
    --output "${RUN_ROOT}/table6-fresh.tsv"
elif [[ -n "${ONLY_ROLE}" || -n "${ONLY_CASE}" || -n "${ONLY_PATTERN}" ]]; then
  repro_note "partial Table 6 run complete; omit --case/--pattern/--only-role to generate the 27-row paper summary"
fi
