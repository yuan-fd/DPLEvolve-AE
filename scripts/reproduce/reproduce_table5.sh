#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

THREADS="${THREADS:-10}"
CHECK_MODE=""
SKIP_PREPARE=0
ROWS=(aes_dense_n45 jpeg_dense_n45 swerv_dense_n45)

usage() {
  cat <<'EOF'
Usage: reproduce_table5.sh [options]

Prepare the three deleted dense placement inputs, build the two exact
candidate source commits for each Table 5 row, and run both candidates through
the same downstream DPO/final flow.  The output is derived from new
metrics.json files, not the archived Table 5 transcription.

Options:
  --threads N          Build/evaluation threads. Default: 10.
  --skip-prepare       Reuse existing DENSE/DENSE_2 ODBs.
  --check-paper-data   Check six source trees + recovered DENSE_2 config.
  --check-inputs       Also check regenerated ORFS ODB/SDC inputs.
  --dry-run            Print commands without executing them.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --threads) THREADS="$2"; shift 2 ;;
    --skip-prepare) SKIP_PREPARE=1; shift ;;
    --check-paper-data) CHECK_MODE=paper-data; shift ;;
    --check-inputs) CHECK_MODE=inputs; shift ;;
    --dry-run) REPRO_DRY_RUN=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) repro_die "unknown argument: $1" ;;
  esac
done
repro_positive_integer threads "${THREADS}"

case_for_row() {
  case "$1" in
    aes_dense_n45) echo aes_dense_nangate45 ;;
    jpeg_dense_n45) echo jpeg_util90_nangate45 ;;
    swerv_dense_n45) echo swerv_wrapper_nangate45 ;;
    *) repro_die "unknown Table 5 row: $1" ;;
  esac
}

variant_for_row() {
  case "$1" in
    aes_dense_n45|jpeg_dense_n45) echo DENSE ;;
    swerv_dense_n45) echo DENSE_2 ;;
  esac
}

missing=0
if [[ ! -f "${PAPER_DATA_ROOT}/table5/MANIFEST.sha256" ]]; then
  printf '[MISSING] %s\n' "${PAPER_DATA_ROOT}/table5/MANIFEST.sha256" >&2
  missing=$((missing + 1))
fi
for row_id in "${ROWS[@]}"; do
  for role in selected reference; do
    path="${PAPER_DATA_ROOT}/table5/${row_id}/programs/${role}/dpl_evolve/CMakeLists.txt"
    if [[ ! -f "${path}" ]]; then
      printf '[MISSING] %s\n' "${path}" >&2
      missing=$((missing + 1))
    fi
  done
done
swerv_config="${PAPER_DATA_ROOT}/table5/swerv_dense_n45/input/config_dense2.mk"
if [[ ! -f "${swerv_config}" ]]; then
  printf '[MISSING] %s\n' "${swerv_config}" >&2
  missing=$((missing + 1))
fi
if [[ "${missing}" -eq 0 ]]; then
  if ! "${DPL_EVOLVE_PYTHON}" "${SCRIPT_DIR}/verify_data_manifest.py" \
    --root "${PAPER_DATA_ROOT}" --scope table5; then
    echo "[BLOCKED] Table 5 source-package checksum verification failed" >&2
    exit 3
  fi
fi
if [[ "${missing}" -gt 0 ]]; then
  echo "[BLOCKED] Table 5 recovery is incomplete: six source commits and the untracked SWERV config_dense2.mk were not retained." >&2
  echo "          See configs/reproduction/table5-{sources,inputs}.tsv and docs/paper-data-layout.md." >&2
  exit 3
fi
if [[ "${CHECK_MODE}" == paper-data ]]; then
  echo "[PASS] all six Table 5 source trees and the SWERV DENSE_2 config are present and checksummed"
  exit 0
fi

if [[ "${CHECK_MODE}" == inputs ]]; then
  for row_id in "${ROWS[@]}"; do
    case_id="$(case_for_row "${row_id}")"
    flow_variant="$(variant_for_row "${row_id}")"
    platform="$(${DPL_EVOLVE_PYTHON} "${DPL_EVOLVE_AGENT_ROOT}/scripts/repo/case_registry.py" --case "${case_id}" --field platform)"
    design="$(${DPL_EVOLVE_PYTHON} "${DPL_EVOLVE_AGENT_ROOT}/scripts/repo/case_registry.py" --case "${case_id}" --field design)"
    for filename in 3_4_place_resized.odb 2_floorplan.sdc; do
      path="${ORFS_ROOT}/flow/results/${platform}/${design}/${flow_variant}/${filename}"
      if [[ ! -f "${path}" ]]; then printf '[MISSING] %s\n' "${path}" >&2; missing=$((missing + 1)); fi
    done
  done
  if [[ "${missing}" -gt 0 ]]; then
    echo "[BLOCKED] Regenerate Table 5 inputs with 'make prepare-table5-inputs'." >&2
    exit 3
  fi
  echo "[PASS] Table 5 sources and regenerated dense inputs are present"
  exit 0
fi

if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then repro_require_runtime; fi
if [[ "${SKIP_PREPARE}" -eq 0 ]]; then
  prepare_args=(bash "${SCRIPT_DIR}/prepare_table5_inputs.sh" --threads "${THREADS}")
  if [[ "${REPRO_DRY_RUN}" -eq 1 ]]; then prepare_args+=(--dry-run); fi
  "${prepare_args[@]}"
fi

RUN_ID="table5_$(date +%Y%m%d_%H%M%S)"
RUN_ROOT="${REPRO_OUTPUT_ROOT}/${RUN_ID}"
MANIFEST="${RUN_ROOT}/runs.tsv"
if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then
  mkdir -p "${RUN_ROOT}"
  printf 'experiment\trow_id\trole\tcase\tpattern\tresults_tsv\n' > "${MANIFEST}"
fi

for row_id in "${ROWS[@]}"; do
  case_id="$(case_for_row "${row_id}")"
  flow_variant="$(variant_for_row "${row_id}")"
  plan="${RUN_ROOT}/${row_id}.tsv"
  if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then
    printf 'enabled\tcase\tcore_utilization\tflow_variant\tround_id\tstart_kind\tnotes\n1\t%s\tdefault\t%s\t%s\tfrozen\tTable 5 exact source replay\n' \
      "${case_id}" "${flow_variant}" "${RUN_ID}" > "${plan}"
  else
    repro_note "would write one-row plan ${plan}: case=${case_id} flow_variant=${flow_variant}"
  fi

  for role in selected reference; do
    matrix_id="${row_id}_${role}"
    output_root="${RUN_ROOT}/matrices"
    results="${output_root}/${matrix_id}/results.tsv"
    repro_run "${DPL_EVOLVE_AGENT_ROOT}/scripts/matrix/run_candidate_matrix.sh" \
      --matrix-id "${matrix_id}" \
      --output-root "${output_root}" \
      --candidate-src "${PAPER_DATA_ROOT}/table5/${row_id}/programs/${role}/dpl_evolve" \
      --candidate-label "${role}" \
      --plan "${plan}" \
      --threads "${THREADS}" \
      --max-parallel 1 \
      --skip-place \
      --skip-baseline
    if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then
      printf 'table5\t%s\t%s\t%s\t\t%s\n' "${row_id}" "${role}" "${case_id}" "${results}" >> "${MANIFEST}"
    fi
  done
done

if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then
  fresh_runs="${RUN_ROOT}/table5-fresh-runs.tsv"
  repro_run "${DPL_EVOLVE_PYTHON}" "${SCRIPT_DIR}/summarize_replays.py" \
    --manifest "${MANIFEST}" --output "${fresh_runs}"
  repro_run "${DPL_EVOLVE_PYTHON}" "${SCRIPT_DIR}/summarize_table5.py" \
    --fresh-runs "${fresh_runs}" --output "${RUN_ROOT}/table5-fresh.tsv"
fi
