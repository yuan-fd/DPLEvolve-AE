#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

THREADS="${THREADS:-10}"
TIMEOUT_SECONDS="7200"
CHECK_ONLY=0
CHECK_GENERATED_SEEDS=1
ROWS=(
  ariane133_nangate45:center-8um:evolved_negotiation_selected
  ariane133_nangate45:center-9um:evolved_negotiation_default_fail_probe8
  ariane133_nangate45:center-10um:evolved_negotiation_default_fail_probe8
  swerv_wrapper_nangate45:center-5.25um:evolved_negotiation_selected
  swerv_wrapper_nangate45:center-5.5um:evolved_negotiation_default_fail_probe8
  swerv_wrapper_nangate45:center-6um:evolved_negotiation_default_fail_probe8
  bp_quad_nangate45:center-5um:evolved_negotiation_bpquad_center_probe
  bp_quad_nangate45:center-6um:evolved_negotiation_bpquad_center_probe
  bp_quad_nangate45:center-8um:evolved_negotiation_bpquad_center_probe
)
PROGRAMS=(diamond negotiation evolved_negotiation_selected evolved_negotiation_default_fail_probe8 evolved_negotiation_bpquad_center_probe)

usage() {
  cat <<'EOF'
Usage: reproduce_table6.sh [--threads N] [--check-inputs | --check-paper-data] [--dry-run]

Stage each exact cut-row ODB, build the fixed Diamond and Negotiation sources
and the three reported ReviewDSE repair sources, then execute all nine patterns
with the paper's 7200-second per-flow cap. Pass/fail comes from fresh OpenROAD
and check_placement results.

--check-paper-data verifies only the separately distributed, checksummed
assets. --check-inputs additionally requires the seed sources made by
`make bootstrap`.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --threads) THREADS="$2"; shift 2 ;;
    --check-inputs) CHECK_ONLY=1; shift ;;
    --check-paper-data) CHECK_ONLY=1; CHECK_GENERATED_SEEDS=0; shift ;;
    --dry-run) REPRO_DRY_RUN=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) repro_die "unknown argument: $1" ;;
  esac
done
repro_positive_integer threads "${THREADS}"

missing=0
if [[ ! -f "${PAPER_DATA_ROOT}/table6/MANIFEST.sha256" ]]; then
  printf '[MISSING] %s\n' "${PAPER_DATA_ROOT}/table6/MANIFEST.sha256" >&2
  missing=$((missing + 1))
fi
for row in "${ROWS[@]}"; do
  IFS=: read -r case_id pattern program <<< "${row}"
  for path in \
    "${PAPER_DATA_ROOT}/table6/inputs/${case_id}/${pattern}/3_4_place_resized.odb" \
    "${PAPER_DATA_ROOT}/table6/inputs/${case_id}/${pattern}/2_floorplan.sdc"; do
    if [[ ! -f "${path}" ]]; then printf '[MISSING] %s\n' "${path}" >&2; missing=$((missing + 1)); fi
  done
done
for program in "${PROGRAMS[@]}"; do
  case "${program}" in
    diamond)
      [[ "${CHECK_GENERATED_SEEDS}" -eq 1 ]] || continue
      path="${DPL_EVOLVE_STATE_ROOT}/seed_sources/diamond_dpl_evolve/CMakeLists.txt"
      ;;
    negotiation)
      [[ "${CHECK_GENERATED_SEEDS}" -eq 1 ]] || continue
      path="${DPL_EVOLVE_STATE_ROOT}/seed_sources/default_negotiation_dpl_evolve/CMakeLists.txt"
      ;;
    *) path="${PAPER_DATA_ROOT}/table6/programs/${program}/dpl_evolve/CMakeLists.txt" ;;
  esac
  if [[ ! -f "${path}" ]]; then printf '[MISSING] %s\n' "${path}" >&2; missing=$((missing + 1)); fi
done
if [[ "${missing}" -gt 0 ]]; then
  printf '[BLOCKED] Table 6 needs %s exact input/source files; see docs/paper-data-layout.md\n' "${missing}" >&2
  if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then exit 3; fi
fi
if [[ "${missing}" -eq 0 ]]; then
  if ! "${DPL_EVOLVE_PYTHON}" "${SCRIPT_DIR}/verify_data_manifest.py" \
    --root "${PAPER_DATA_ROOT}" --scope table6; then
    echo "[BLOCKED] Table 6 paper-data checksum verification failed" >&2
    exit 3
  fi
fi
if [[ "${CHECK_ONLY}" -eq 1 ]]; then
  [[ "${missing}" -eq 0 ]] || exit 3
  if [[ "${CHECK_GENERATED_SEEDS}" -eq 1 ]]; then
    echo "[PASS] all Table 6 replay inputs are present"
  else
    echo "[PASS] all external Table 6 paper-data assets are present and checksummed"
  fi
  exit 0
fi
if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then repro_require_runtime; fi

RUN_ID="table6_$(date +%Y%m%d_%H%M%S)"
RUN_ROOT="${REPRO_OUTPUT_ROOT}/${RUN_ID}"
if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then
  mkdir -p "${RUN_ROOT}/plans"
fi

for row in "${ROWS[@]}"; do
  IFS=: read -r case_id pattern program <<< "${row}"
  platform="$(${DPL_EVOLVE_PYTHON} "${DPL_EVOLVE_AGENT_ROOT}/scripts/repo/case_registry.py" --case "${case_id}" --field platform 2>/dev/null || echo nangate45)"
  design="$(${DPL_EVOLVE_PYTHON} "${DPL_EVOLVE_AGENT_ROOT}/scripts/repo/case_registry.py" --case "${case_id}" --field design 2>/dev/null || true)"
  slug="${case_id}_${pattern//./_}"
  flow_variant="paper_table6_${slug}"
  staged_odb="${ORFS_ROOT}/flow/results/${platform}/${design}/${flow_variant}/3_4_place_resized.odb"
  staged_sdc="${ORFS_ROOT}/flow/results/${platform}/${design}/${flow_variant}/2_floorplan.sdc"
  repro_run install -D -m 0644 "${PAPER_DATA_ROOT}/table6/inputs/${case_id}/${pattern}/3_4_place_resized.odb" "${staged_odb}"
  repro_run install -D -m 0644 "${PAPER_DATA_ROOT}/table6/inputs/${case_id}/${pattern}/2_floorplan.sdc" "${staged_sdc}"
done

run_program() {
  local program="$1"
  local role="$2"
  local plan="${RUN_ROOT}/plans/${program}.tsv"
  local row case_id pattern selected_program slug flow_variant matrix_id candidate_src
  case "${program}" in
    diamond) candidate_src="${DPL_EVOLVE_STATE_ROOT}/seed_sources/diamond_dpl_evolve" ;;
    negotiation) candidate_src="${DPL_EVOLVE_STATE_ROOT}/seed_sources/default_negotiation_dpl_evolve" ;;
    *) candidate_src="${PAPER_DATA_ROOT}/table6/programs/${program}/dpl_evolve" ;;
  esac
  if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then
    printf 'enabled\tcase\tcore_utilization\tflow_variant\tround_id\tstart_kind\tnotes\n' > "${plan}"
  fi
  for row in "${ROWS[@]}"; do
    IFS=: read -r case_id pattern selected_program <<< "${row}"
    if [[ "${role}" == reviewdse && "${selected_program}" != "${program}" ]]; then continue; fi
    slug="${case_id}_${pattern//./_}"
    flow_variant="paper_table6_${slug}"
    if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then
      printf '1\t%s\tdefault\t%s\t%s\tfrozen\t%s\n' "${case_id}" "${flow_variant}" "${RUN_ID}" "${pattern}" >> "${plan}"
    fi
  done
  if [[ "${REPRO_DRY_RUN}" -eq 1 ]]; then repro_note "would write filtered plan ${plan} for ${program}"; fi
  matrix_id="table6_${program}"
  repro_run "${DPL_EVOLVE_AGENT_ROOT}/scripts/matrix/run_candidate_matrix.sh" \
    --matrix-id "${matrix_id}" \
    --output-root "${RUN_ROOT}/matrices" \
    --candidate-src "${candidate_src}" \
    --candidate-label "${program}" \
    --plan "${plan}" \
    --threads "${THREADS}" \
    --max-parallel 1 \
    --skip-place \
    --skip-baseline \
    --legalize-timeout-seconds "${TIMEOUT_SECONDS}"
}

run_program diamond fixed
run_program negotiation fixed
run_program evolved_negotiation_selected reviewdse
run_program evolved_negotiation_default_fail_probe8 reviewdse
run_program evolved_negotiation_bpquad_center_probe reviewdse

if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then
  repro_run "${DPL_EVOLVE_PYTHON}" "${SCRIPT_DIR}/summarize_table6.py" \
    --matrix-root "${RUN_ROOT}/matrices" \
    --experiment-manifest "${AE_ROOT}/configs/reproduction/paper-experiments.json" \
    --output "${RUN_ROOT}/table6-fresh.tsv"
fi
